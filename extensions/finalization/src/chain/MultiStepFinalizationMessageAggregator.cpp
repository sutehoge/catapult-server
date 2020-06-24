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
#include "finalization/src/model/FinalizationMessage.h"

namespace catapult { namespace chain {

	MultiStepFinalizationMessageAggregator::MultiStepFinalizationMessageAggregator(
			const finalization::FinalizationConfiguration& config,
			const AggregatorFactory& aggregatorFactory,
			const ConsensusSink& consensusSink)
			: m_config(config)
			, m_aggregatorFactory(aggregatorFactory)
			, m_consensusSink(consensusSink)
	{}

	size_t MultiStepFinalizationMessageAggregator::size() const {
		return m_aggregators.size();
	}

	void MultiStepFinalizationMessageAggregator::add(const model::FinalizationMessage& message, uint64_t numVotes) {
		if (!canAccept(message.StepIdentifier))
			return;

		auto iter = m_aggregators.find(message.StepIdentifier);
		if (m_aggregators.end() == iter)
			iter = m_aggregators.emplace(message.StepIdentifier, m_aggregatorFactory(m_config)).first;

		if (add(*iter->second, message, numVotes))
			m_aggregators.erase(m_aggregators.begin(), iter);
	}

	bool MultiStepFinalizationMessageAggregator::canAccept(const crypto::StepIdentifier& stepIdentifier) {
		if (m_aggregators.empty())
			return true;

		// only accept new messages for a step no less than the last consensus step
		const auto& aggregatePair = *m_aggregators.cbegin();
		return stepIdentifier >= aggregatePair.first || !aggregatePair.second->hasConsensus();
	}

	bool MultiStepFinalizationMessageAggregator::add(
			SingleStepFinalizationMessageAggregator& aggregator,
			const model::FinalizationMessage& message,
			uint64_t numVotes) {
		aggregator.add(message, numVotes);
		if (!aggregator.hasConsensus())
			return false;

		m_consensusSink(message.StepIdentifier, aggregator.consensusHash());
		return true;
	}
}}
