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

	MultiStepFinalizationMessageAggregator::MultiStepFinalizationMessageAggregator(
			const MessageProcessor& messageProcessor,
			const SingleStepAggregatorFactory& aggregatorFactory,
			const ConsensusSink& consensusSink)
			: m_messageProcessor(messageProcessor)
			, m_aggregatorFactory(aggregatorFactory)
			, m_consensusSink(consensusSink)
			, m_minStepIdentier({ 0, 0, 0 })
	{}

	size_t MultiStepFinalizationMessageAggregator::size() const {
		return m_stepDataTuplesMap.size();
	}

	void MultiStepFinalizationMessageAggregator::setNextFinalizationPoint(FinalizationPoint point) {
		if (point < m_nextFinalizationPoint)
			CATAPULT_THROW_INVALID_ARGUMENT_1("cannot set finalization point to lower value", m_nextFinalizationPoint);

		if (m_nextFinalizationPoint == point)
			return;

		m_minStepIdentier = { point.unwrap(), 0, 0 };
		m_nextFinalizationPoint = point;
		m_stepDataTuplesMap.clear();
	}

	void MultiStepFinalizationMessageAggregator::add(const std::shared_ptr<model::FinalizationMessage>& pMessage) {
		const auto& stepIdentifier = pMessage->StepIdentifier;
		if (!canAccept(stepIdentifier))
			return;

		auto processResultPair = process(*pMessage);
		if (!processResultPair.second)
			return;

		auto iter = m_stepDataTuplesMap.find(stepIdentifier);
		if (m_stepDataTuplesMap.end() == iter) {
			iter = m_stepDataTuplesMap.emplace(stepIdentifier, StepDataTuple()).first;
			iter->second.pAggregator = m_aggregatorFactory(pMessage->StepIdentifier);
		}

		iter->second.Proof.push_back(pMessage);

		if (add(iter->second, *pMessage, processResultPair.first)) {
			// new consensus was reached, so drop older messages
			m_minStepIdentier = iter->first;
			m_stepDataTuplesMap.erase(m_stepDataTuplesMap.begin(), iter);
		}
	}

	bool MultiStepFinalizationMessageAggregator::canAccept(const crypto::StepIdentifier& stepIdentifier) {
		// only accept messages for the current FP that are no less than the min consensus step
		return m_nextFinalizationPoint == FinalizationPoint(stepIdentifier.Point) && stepIdentifier >= m_minStepIdentier;
	}

	bool MultiStepFinalizationMessageAggregator::add(
			StepDataTuple& stepDataTuple,
			const model::FinalizationMessage& message,
			uint64_t numVotes) {
		stepDataTuple.pAggregator->add(message, numVotes);
		if (!stepDataTuple.pAggregator->hasConsensus())
			return false;

		m_consensusSink(message.StepIdentifier, stepDataTuple.pAggregator->consensusHash(), stepDataTuple.Proof);
		return true;
	}

	std::pair<uint64_t, bool> MultiStepFinalizationMessageAggregator::process(const model::FinalizationMessage& message) {
		auto processResultPair = m_messageProcessor(message);
		if (model::ProcessMessageResult::Success != processResultPair.first) {
			CATAPULT_LOG(warning) << "rejecting finalization message with result " << processResultPair.first;
			return std::make_pair(0, false);
		}

		return std::make_pair(processResultPair.second, true);
	}
}}
