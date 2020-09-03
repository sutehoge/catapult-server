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

#include "MultiRoundMessageAggregator.h"
#include "RoundContext.h"
#include "finalization/src/model/FinalizationMessage.h"

namespace catapult { namespace chain {

	// region MultiRoundMessageAggregatorState

	struct MultiRoundMessageAggregatorState {
	public:
		MultiRoundMessageAggregatorState(
				uint64_t maxResponseSize,
				const model::FinalizationRound& round,
				const model::HeightHashPair& previousFinalizedHeightHashPair,
				const MultiRoundMessageAggregator::RoundMessageAggregatorFactory& roundMessageAggregatorFactory)
				: MaxResponseSize(maxResponseSize)
				, MinFinalizationRound(round)
				, MaxFinalizationRound(round)
				, PreviousFinalizedHeightHashPair(previousFinalizedHeightHashPair)
				, RoundMessageAggregatorFactory(roundMessageAggregatorFactory)
		{}

	public:
		uint64_t MaxResponseSize;
		model::FinalizationRound MinFinalizationRound;
		model::FinalizationRound MaxFinalizationRound;
		model::HeightHashPair PreviousFinalizedHeightHashPair;
		MultiRoundMessageAggregator::RoundMessageAggregatorFactory RoundMessageAggregatorFactory;
		std::map<model::FinalizationRound, std::unique_ptr<RoundMessageAggregator>> RoundMessageAggregators;
	};

	// endregion

	// region MultiRoundMessageAggregatorView

	MultiRoundMessageAggregatorView::MultiRoundMessageAggregatorView(
			const MultiRoundMessageAggregatorState& state,
			utils::SpinReaderWriterLock::ReaderLockGuard&& readLock)
			: m_state(state)
			, m_readLock(std::move(readLock))
	{}

	size_t MultiRoundMessageAggregatorView::size() const {
		return m_state.RoundMessageAggregators.size();
	}

	model::FinalizationRound MultiRoundMessageAggregatorView::minFinalizationRound() const {
		return m_state.MinFinalizationRound;
	}

	model::FinalizationRound MultiRoundMessageAggregatorView::maxFinalizationRound() const {
		return m_state.MaxFinalizationRound;
	}

	const RoundContext* MultiRoundMessageAggregatorView::tryGetRoundContext(const model::FinalizationRound& round) const {
		auto iter = m_state.RoundMessageAggregators.find(round);
		return m_state.RoundMessageAggregators.cend() == iter ? nullptr : &iter->second->roundContext();
	}

	model::HeightHashPair MultiRoundMessageAggregatorView::findEstimate(const model::FinalizationRound& round) const {
		const auto& roundMessageAggregators = m_state.RoundMessageAggregators;
		for (auto iter = roundMessageAggregators.crbegin(); roundMessageAggregators.crend() != iter; ++iter) {
			if (iter->first > round)
				continue;

			auto estimateResultPair = iter->second->roundContext().tryFindEstimate();
			if (estimateResultPair.second)
				return estimateResultPair.first;
		}

		return m_state.PreviousFinalizedHeightHashPair;
	}

	BestPrecommitDescriptor MultiRoundMessageAggregatorView::tryFindBestPrecommit() const {
		const auto& roundMessageAggregators = m_state.RoundMessageAggregators;
		for (auto iter = roundMessageAggregators.crbegin(); roundMessageAggregators.crend() != iter; ++iter) {
			auto bestPrecommitResultPair = iter->second->roundContext().tryFindBestPrecommit();
			if (bestPrecommitResultPair.second) {
				BestPrecommitDescriptor descriptor;
				descriptor.Round = iter->first;
				descriptor.Target = bestPrecommitResultPair.first;
				descriptor.Proof = iter->second->unknownMessages({});
				return descriptor;
			}
		}

		return BestPrecommitDescriptor();
	}

	model::ShortHashRange MultiRoundMessageAggregatorView::shortHashes() const {
		std::vector<utils::ShortHash> shortHashes;
		for (const auto& pair : m_state.RoundMessageAggregators) {
			auto roundShortHashes = pair.second->shortHashes();
			shortHashes.insert(shortHashes.end(), roundShortHashes.cbegin(), roundShortHashes.cend());
		}

		return model::ShortHashRange::CopyFixed(reinterpret_cast<const uint8_t*>(shortHashes.data()), shortHashes.size());
	}

	RoundMessageAggregator::UnknownMessages MultiRoundMessageAggregatorView::unknownMessages(
			const model::FinalizationRound& round,
			const utils::ShortHashesSet& knownShortHashes) const {
		CATAPULT_LOG(debug) << "<FIN:debug> finding unknownMessages for round " << round;

		uint64_t totalSize = 0;
		RoundMessageAggregator::UnknownMessages allMessages;
		for (const auto& pair : m_state.RoundMessageAggregators) {
			if (pair.first < round) {
				CATAPULT_LOG(debug) << "<FIN:debug> skipping aggregator with " << round;
				continue;
			}

			for (const auto& pMessage : pair.second->unknownMessages(knownShortHashes)) {
				totalSize += pMessage->Size;
				if (totalSize > m_state.MaxResponseSize) {
					CATAPULT_LOG(debug) << "<FIN:debug> returning " << allMessages.size() << "messages (limit)";
					return allMessages;
				}

				allMessages.push_back(pMessage);
			}
		}

		CATAPULT_LOG(debug) << "<FIN:debug> returning " << allMessages.size() << "messages (all)";
		return allMessages;
	}

	// endregion

	// region MultiRoundMessageAggregatorModifier

	MultiRoundMessageAggregatorModifier::MultiRoundMessageAggregatorModifier(
			MultiRoundMessageAggregatorState& state,
			utils::SpinReaderWriterLock::WriterLockGuard&& writeLock)
			: m_state(state)
			, m_writeLock(std::move(writeLock))
	{}

	void MultiRoundMessageAggregatorModifier::setMaxFinalizationRound(const model::FinalizationRound& round) {
		if (m_state.MinFinalizationRound > round)
			CATAPULT_THROW_INVALID_ARGUMENT("cannot set max finalization round below min");

		CATAPULT_LOG(debug) << "<FIN> setting max finalization round to " << round;
		m_state.MaxFinalizationRound = round;
	}

	RoundMessageAggregatorAddResult MultiRoundMessageAggregatorModifier::add(const std::shared_ptr<model::FinalizationMessage>& pMessage) {
		auto messageRound = model::FinalizationRound{ pMessage->StepIdentifier.Epoch, pMessage->StepIdentifier.Point };
		if (m_state.MinFinalizationRound > messageRound || m_state.MaxFinalizationRound < messageRound) {
			CATAPULT_LOG(warning)
					<< "rejecting message with round " << messageRound
					<< ", min round " << m_state.MinFinalizationRound
					<< ", max round " << m_state.MaxFinalizationRound;
			return RoundMessageAggregatorAddResult::Failure_Invalid_Point;
		}

		auto iter = m_state.RoundMessageAggregators.find(messageRound);
		if (m_state.RoundMessageAggregators.cend() == iter) {
			auto pRoundAggregator = m_state.RoundMessageAggregatorFactory(messageRound);
			iter = m_state.RoundMessageAggregators.emplace(messageRound, std::move(pRoundAggregator)).first;
		}

		CATAPULT_LOG(debug) << "<FIN> adding message to aggregator at " << messageRound << " with height " << pMessage->Height;
		return iter->second->add(pMessage);
	}

	void MultiRoundMessageAggregatorModifier::prune() {
		auto& roundMessageAggregators = m_state.RoundMessageAggregators;

		auto lastMatchingIter = roundMessageAggregators.cend();
		for (auto iter = roundMessageAggregators.cbegin(); roundMessageAggregators.cend() != iter; ++iter) {
			if (iter->second->roundContext().tryFindBestPrecommit().second)
				lastMatchingIter = iter;
		}

		if (roundMessageAggregators.cend() == lastMatchingIter)
			return;

		auto prevLastMatchingIter = lastMatchingIter;
		while (roundMessageAggregators.cbegin() != prevLastMatchingIter) {
			--prevLastMatchingIter;

			auto estimateResultPair = prevLastMatchingIter->second->roundContext().tryFindEstimate();
			if (estimateResultPair.second) {
				m_state.PreviousFinalizedHeightHashPair = estimateResultPair.first;
				break;
			}
		}

		CATAPULT_LOG(debug) << "<FIN> pruning  MultiRoundMessageAggregator: " << roundMessageAggregators.cbegin()->first;
		roundMessageAggregators.erase(roundMessageAggregators.cbegin(), lastMatchingIter);
		m_state.MinFinalizationRound = lastMatchingIter->first;
	}

	// endregion

	// region MultiRoundMessageAggregator

	MultiRoundMessageAggregator::MultiRoundMessageAggregator(
			uint64_t maxResponseSize,
			const model::FinalizationRound& round,
			const model::HeightHashPair& previousFinalizedHeightHashPair,
			const RoundMessageAggregatorFactory& roundMessageAggregatorFactory)
			: m_pState(std::make_unique<MultiRoundMessageAggregatorState>(
					maxResponseSize,
					round,
					previousFinalizedHeightHashPair,
					roundMessageAggregatorFactory)) {
		CATAPULT_LOG(debug) << "creating multi round message aggregator starting at round " << round;
	}

	MultiRoundMessageAggregator::~MultiRoundMessageAggregator() = default;

	MultiRoundMessageAggregatorView MultiRoundMessageAggregator::view() const {
		auto readLock = m_lock.acquireReader();
		return MultiRoundMessageAggregatorView(*m_pState, std::move(readLock));
	}

	MultiRoundMessageAggregatorModifier MultiRoundMessageAggregator::modifier() {
		auto writeLock = m_lock.acquireWriter();
		return MultiRoundMessageAggregatorModifier(*m_pState, std::move(writeLock));
	}

	// endregion
}}
