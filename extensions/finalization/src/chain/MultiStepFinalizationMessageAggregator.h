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
#include "catapult/utils/SpinReaderWriterLock.h"
#include <map>

namespace catapult {
	namespace chain {
		struct MultiStepFinalizationMessageAggregatorState;
		struct StepDataTuple;
	}
}

namespace catapult { namespace chain {

	// region MultiStepFinalizationMessageAggregatorView

	/// Read only view on top of multi step finalization message aggregator.
	class MultiStepFinalizationMessageAggregatorView : utils::MoveOnly {
	private:
		using UnknownMessages = std::vector<std::shared_ptr<const model::FinalizationMessage>>;

	public:
		/// Creates a view around \a state with lock context \a readLock.
		MultiStepFinalizationMessageAggregatorView(
				const MultiStepFinalizationMessageAggregatorState& state,
				utils::SpinReaderWriterLock::ReaderLockGuard&& readLock);

	public:
		/// Gets the number of step identifiers currently tracked.
		size_t size() const;

		/// Gets the minimum step identifier that is currently tracked.
		crypto::StepIdentifier minStepIdentifier() const;

		/// Gets a range of short hashes of all messages in the cache.
		/// \note Each short hash consists of the first 4 bytes of the complete hash.
		model::ShortHashRange shortHashes() const;

		/// Gets all finalization messages starting at \a stepIdentifier that do not have a short hash in \a knownShortHashes.
		UnknownMessages unknownMessages(const crypto::StepIdentifier& stepIdentifier, const utils::ShortHashesSet& knownShortHashes) const;

	private:
		const MultiStepFinalizationMessageAggregatorState& m_state;
		utils::SpinReaderWriterLock::ReaderLockGuard m_readLock;
	};

	// endregion

	// region MultiStepFinalizationMessageAggregatorModifier

	/// Write only view on top of multi step finalization message aggregator.
	class MultiStepFinalizationMessageAggregatorModifier : utils::MoveOnly {
	public:
		/// Creates a view around \a state with lock context \a writeLock.
		MultiStepFinalizationMessageAggregatorModifier(
				MultiStepFinalizationMessageAggregatorState& state,
				utils::SpinReaderWriterLock::WriterLockGuard&& writeLock);

	public:
		/// Sets the next finalization \a point.
		/// \note Only messages with a matching finalization point will be processed immediately.
		void setNextFinalizationPoint(FinalizationPoint point);

		/// Adds a finalization message (\a pMessage) to the aggregator.
		/// \note Message is a shared_ptr because it is detached from an EntityRange and is kept alive with its associated step.
		void add(const std::shared_ptr<model::FinalizationMessage>& pMessage);

	private:
		bool canAccept(const crypto::StepIdentifier& stepIdentifier);

		std::pair<uint64_t, bool> process(const model::FinalizationMessage& message);

		bool add(StepDataTuple& stepDataTuple, const model::FinalizationMessage& message, uint64_t numVotes);

	private:
		MultiStepFinalizationMessageAggregatorState& m_state;
		utils::SpinReaderWriterLock::WriterLockGuard m_writeLock;
	};

	// endregion

	// region MultiStepFinalizationMessageAggregator

	/// Aggregates finalization messages across multiple steps until consensus is reached.
	class MultiStepFinalizationMessageAggregator {
	public:
		/// Creates an aggregator around \a maxResponseSize, \a messageProcessor, \a aggregatorFactory and \a consensusSink.
		MultiStepFinalizationMessageAggregator(
				uint64_t maxResponseSize,
				const MessageProcessor& messageProcessor,
				const SingleStepAggregatorFactory& aggregatorFactory,
				const ConsensusSink& consensusSink);

		/// Destroys the aggregator.
		~MultiStepFinalizationMessageAggregator();

	public:
		/// Gets a read only view of the aggregator.
		MultiStepFinalizationMessageAggregatorView view() const;

		/// Gets a write only view of the aggregator.
		MultiStepFinalizationMessageAggregatorModifier modifier();

	private:
		std::unique_ptr<MultiStepFinalizationMessageAggregatorState> m_pState;
		mutable utils::SpinReaderWriterLock m_lock;
	};

	// endregion
}}
