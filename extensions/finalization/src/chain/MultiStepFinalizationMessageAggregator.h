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

#pragma once
#include "SingleStepFinalizationMessageAggregator.h"
#include "catapult/crypto_voting/OtsTypes.h"
#include "catapult/functions.h"
#include <map>

namespace catapult { namespace chain {

	/// Aggregates finalization messages across multiple steps until consensus is reached.
	class MultiStepFinalizationMessageAggregator {
	public:
		using AggregatorFactory = std::function<std::unique_ptr<SingleStepFinalizationMessageAggregator> (
				const finalization::FinalizationConfiguration&)>;
		using ConsensusSink = consumer<const crypto::StepIdentifier&, const Hash256&>;

	public:
		/// Creates an aggregator around \a config, \a aggregatorFactory and \a consensusSink.
		MultiStepFinalizationMessageAggregator(
				const finalization::FinalizationConfiguration& config,
				const AggregatorFactory& aggregatorFactory,
				const ConsensusSink& consensusSink);

	public:
		/// Gets the number of step identifiers currently tracked.
		size_t size() const;

	public:
		/// Adds a finalization \a message to the aggregator that contributes \a numVotes votes.
		/// \note This function is expected to be called after ProcessMessage.
		void add(const model::FinalizationMessage& message, uint64_t numVotes);

	private:
		bool canAccept(const crypto::StepIdentifier& stepIdentifier);

		bool add(SingleStepFinalizationMessageAggregator& aggregator, const model::FinalizationMessage& message, uint64_t numVotes);

	private:
		finalization::FinalizationConfiguration m_config;
		AggregatorFactory m_aggregatorFactory;
		ConsensusSink m_consensusSink;

		std::map<crypto::StepIdentifier, std::unique_ptr<SingleStepFinalizationMessageAggregator>> m_aggregators;
	};
}}
