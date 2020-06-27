/**
*** Copyright (c) 2016-present,
*** Jaguar0625, gimre, BloodyRookie, Tech Bureau, Corp. All rights reserved.
***
*** This file is part of Catapult.
***
*** Catapult is free software: you can redistribute it and/or modify
*** it under the terms of the GNU Lesser General Public License as published by
*** the Free Software Foundation, either version 3 of the License, or
*** (at your option) any later version.
***
*** Catapult is distributed in the hope that it will be useful,
*** but WITHOUT ANY WARRANTY; without even the implied warranty of
*** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*** GNU Lesser General Public License for more details.
***
*** You should have received a copy of the GNU Lesser General Public License
*** along with Catapult. If not, see <http://www.gnu.org/licenses/>.
**/

#include "MultiStepFinalizationMessageAggregator.h"

namespace catapult { namespace chain {

	// region StepDataTuple / MultiStepFinalizationMessageAggregatorState

	struct StepDataTuple {
		std::unique_ptr<SingleStepFinalizationMessageAggregator> pAggregator;
		FinalizationProof Proof;
	};

	struct MultiStepFinalizationMessageAggregatorState {
	public:
		MultiStepFinalizationMessageAggregatorState(
				uint64_t maxResponseSize,
				const MessageProcessor& messageProcessor,
				const SingleStepAggregatorFactory& aggregatorFactory,
				const ConsensusSink& consensusSink)
				: MaxResponseSize(maxResponseSize)
				, MessageProcessor(messageProcessor)
				, AggregatorFactory(aggregatorFactory)
				, ConsensusSink(consensusSink)
				, MinStepIdentifier({ 0, 0, 0 })
		{}

	public:
		uint64_t MaxResponseSize;
		chain::MessageProcessor MessageProcessor;
		SingleStepAggregatorFactory AggregatorFactory;
		chain::ConsensusSink ConsensusSink;

		crypto::StepIdentifier MinStepIdentifier;
		FinalizationPoint NextFinalizationPoint;
		std::map<crypto::StepIdentifier, StepDataTuple> StepDataTuplesMap;
	};

	// endregion

	// region MultiStepFinalizationMessageAggregatorView

	MultiStepFinalizationMessageAggregatorView::MultiStepFinalizationMessageAggregatorView(
			const MultiStepFinalizationMessageAggregatorState& state,
			utils::SpinReaderWriterLock::ReaderLockGuard&& readLock)
			: m_state(state)
			, m_readLock(std::move(readLock))
	{}

	size_t MultiStepFinalizationMessageAggregatorView::size() const {
		return m_state.StepDataTuplesMap.size();
	}

	crypto::StepIdentifier MultiStepFinalizationMessageAggregatorView::minStepIdentifier() const {
		return m_state.MinStepIdentifier;
	}

	model::ShortHashRange MultiStepFinalizationMessageAggregatorView::shortHashes() const {
		auto numMessages = 0u;
		for (const auto& stepDataTuplesPair : m_state.StepDataTuplesMap)
			numMessages += stepDataTuplesPair.second.Proof.size();

		auto shortHashes = model::EntityRange<utils::ShortHash>::PrepareFixed(numMessages);
		auto shortHashesIter = shortHashes.begin();
		for (const auto& stepDataTuplesPair : m_state.StepDataTuplesMap) {
			for (const auto& pMessage : stepDataTuplesPair.second.Proof)
				*shortHashesIter++ = utils::ToShortHash(model::CalculateMessageHash(*pMessage));
		}

		return shortHashes;
	}

	MultiStepFinalizationMessageAggregatorView::UnknownMessages MultiStepFinalizationMessageAggregatorView::unknownMessages(
			const crypto::StepIdentifier& stepIdentifier,
			const utils::ShortHashesSet& knownShortHashes) const {
		uint64_t totalSize = 0;
		UnknownMessages messages;
		for (const auto& stepDataTuplesPair : m_state.StepDataTuplesMap) {
			if (stepDataTuplesPair.first < stepIdentifier)
				continue;

			for (const auto& pMessage : stepDataTuplesPair.second.Proof) {
				auto shortHash = utils::ToShortHash(model::CalculateMessageHash(*pMessage));
				auto iter = knownShortHashes.find(shortHash);
				if (knownShortHashes.cend() == iter) {
					totalSize += pMessage->Size;
					if (totalSize > m_state.MaxResponseSize)
						return messages;

					messages.push_back(pMessage);
				}
			}
		}

		return messages;
	}

	// endregion

	// region MultiStepFinalizationMessageAggregatorModifier

	MultiStepFinalizationMessageAggregatorModifier::MultiStepFinalizationMessageAggregatorModifier(
			MultiStepFinalizationMessageAggregatorState& state,
			utils::SpinReaderWriterLock::WriterLockGuard&& writeLock)
			: m_state(state)
			, m_writeLock(std::move(writeLock))
	{}

	void MultiStepFinalizationMessageAggregatorModifier::setNextFinalizationPoint(FinalizationPoint point) {
		if (point < m_state.NextFinalizationPoint)
			CATAPULT_THROW_INVALID_ARGUMENT_1("cannot set finalization point to lower value", m_state.NextFinalizationPoint);

		if (m_state.NextFinalizationPoint == point)
			return;

		m_state.MinStepIdentifier = { point.unwrap(), 0, 0 };
		m_state.NextFinalizationPoint = point;
		m_state.StepDataTuplesMap.clear();
	}

	void MultiStepFinalizationMessageAggregatorModifier::add(const std::shared_ptr<model::FinalizationMessage>& pMessage) {
		const auto& stepIdentifier = pMessage->StepIdentifier;
		if (!canAccept(stepIdentifier))
			return;

		auto processResultPair = process(*pMessage);
		if (!processResultPair.second)
			return;

		auto iter = m_state.StepDataTuplesMap.find(stepIdentifier);
		if (m_state.StepDataTuplesMap.end() == iter) {
			iter = m_state.StepDataTuplesMap.emplace(stepIdentifier, StepDataTuple()).first;
			iter->second.pAggregator = m_state.AggregatorFactory(pMessage->StepIdentifier);
		}

		iter->second.Proof.push_back(pMessage);

		if (add(iter->second, *pMessage, processResultPair.first)) {
			// new consensus was reached, so drop older messages
			m_state.MinStepIdentifier = iter->first;
			m_state.StepDataTuplesMap.erase(m_state.StepDataTuplesMap.begin(), iter);
		}
	}

	bool MultiStepFinalizationMessageAggregatorModifier::canAccept(const crypto::StepIdentifier& stepIdentifier) {
		// only accept messages for the current FP that are no less than the min consensus step
		return m_state.NextFinalizationPoint == FinalizationPoint(stepIdentifier.Point) && stepIdentifier >= m_state.MinStepIdentifier;
	}

	bool MultiStepFinalizationMessageAggregatorModifier::add(
			StepDataTuple& stepDataTuple,
			const model::FinalizationMessage& message,
			uint64_t numVotes) {
		stepDataTuple.pAggregator->add(message, numVotes);
		if (!stepDataTuple.pAggregator->hasConsensus())
			return false;

		m_state.ConsensusSink(message.StepIdentifier, stepDataTuple.pAggregator->consensusHash(), stepDataTuple.Proof);
		return true;
	}

	std::pair<uint64_t, bool> MultiStepFinalizationMessageAggregatorModifier::process(const model::FinalizationMessage& message) {
		auto processResultPair = m_state.MessageProcessor(message);
		if (model::ProcessMessageResult::Success != processResultPair.first) {
			CATAPULT_LOG(warning) << "rejecting finalization message with result " << processResultPair.first;
			return std::make_pair(0, false);
		}

		return std::make_pair(processResultPair.second, true);
	}

	// endregion

	// region MultiStepFinalizationMessageAggregator

	MultiStepFinalizationMessageAggregator::MultiStepFinalizationMessageAggregator(
			uint64_t maxResponseSize,
			const MessageProcessor& messageProcessor,
			const SingleStepAggregatorFactory& aggregatorFactory,
			const ConsensusSink& consensusSink)
			: m_pState(std::make_unique<MultiStepFinalizationMessageAggregatorState>(
					maxResponseSize,
					messageProcessor,
					aggregatorFactory,
					consensusSink))
	{}

	MultiStepFinalizationMessageAggregator::~MultiStepFinalizationMessageAggregator() = default;

	MultiStepFinalizationMessageAggregatorView MultiStepFinalizationMessageAggregator::view() const {
		auto readLock = m_lock.acquireReader();
		return MultiStepFinalizationMessageAggregatorView(*m_pState, std::move(readLock));
	}

	MultiStepFinalizationMessageAggregatorModifier MultiStepFinalizationMessageAggregator::modifier() {
		auto writeLock = m_lock.acquireWriter();
		return MultiStepFinalizationMessageAggregatorModifier(*m_pState, std::move(writeLock));
	}

	// endregion
}}
