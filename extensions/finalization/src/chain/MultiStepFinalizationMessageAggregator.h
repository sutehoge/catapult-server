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
#include "FinalizationAggregatorTypes.h"
#include <map>

namespace catapult { namespace chain {

	/// Aggregates finalization messages across multiple steps until consensus is reached.
	class MultiStepFinalizationMessageAggregator {
	public:
		/// Creates an aggregator around \a messageProcessor, \a aggregatorFactory and \a consensusSink.
		MultiStepFinalizationMessageAggregator(
				const MessageProcessor& messageProcessor,
				const SingleStepAggregatorFactory& aggregatorFactory,
				const ConsensusSink& consensusSink);

	public:
		/// Gets the number of step identifiers currently tracked.
		size_t size() const;

	public:
		/// Sets the next finalization \a point.
		/// \note Only messages with a matching finalization point will be processed immediately.
		void setNextFinalizationPoint(FinalizationPoint point);

		/// Adds a finalization message (\a pMessage) to the aggregator.
		/// \note Message is a shared_ptr because it is detached from an EntityRange and is kept alive with its associated step.
		void add(const std::shared_ptr<model::FinalizationMessage>& pMessage);

	private:
		struct StepDataTuple {
			std::unique_ptr<SingleStepFinalizationMessageAggregator> pAggregator;
			FinalizationProof Proof;
		};

	private:
		bool canAccept(const crypto::StepIdentifier& stepIdentifier);

		std::pair<uint64_t, bool> process(const model::FinalizationMessage& message);

		bool add(StepDataTuple& stepDataTuple, const model::FinalizationMessage& message, uint64_t numVotes);

	private:
		MessageProcessor m_messageProcessor;
		SingleStepAggregatorFactory m_aggregatorFactory;
		ConsensusSink m_consensusSink;

		crypto::StepIdentifier m_minStepIdentier;
		FinalizationPoint m_nextFinalizationPoint;
		std::map<crypto::StepIdentifier, StepDataTuple> m_stepDataTuplesMap;
	};
}}
