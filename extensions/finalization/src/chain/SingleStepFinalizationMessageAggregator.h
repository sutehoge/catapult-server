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
#include "catapult/types.h"
#include <memory>

namespace catapult {
	namespace finalization { struct FinalizationConfiguration; }
	namespace model { struct FinalizationMessage; }
}

namespace catapult { namespace chain {

	/// Finalization proof.
	using FinalizationProof = std::vector<std::shared_ptr<const model::FinalizationMessage>>;

	/// Aggregates finalization messages for a single step until consensus is reached.
	/// \note Messages are assumed to all refer to the same step identifier and be validated by the caller.
	class SingleStepFinalizationMessageAggregator {
	public:
		virtual ~SingleStepFinalizationMessageAggregator() = default;

	public:
		/// Returns \c true if consensus has been reached.
		virtual bool hasConsensus() const = 0;

		/// Gets the consensus height.
		virtual Height consensusHeight() const = 0;

		/// Gets the consensus hash.
		virtual Hash256 consensusHash() const = 0;

	public:
		/// Reduces \a proof by removing superfluous messages.
		/// \note This allows an aggregator to pick a best message.
		virtual void reduce(FinalizationProof& proof) = 0;

		/// Adds a finalization \a message to the aggregator that contributes \a numVotes votes.
		/// \note This function is expected to be called after ProcessMessage.
		virtual void add(const model::FinalizationMessage& message, uint64_t numVotes) = 0;
	};

	/// Creates a finalization message aggregator that picks the message with the maximum number of votes given \a config.
	/// \note This "aggregator" always reaches initial consensus after the first message is received.
	std::unique_ptr<SingleStepFinalizationMessageAggregator> CreateFinalizationMessageMaximumVotesAggregator(
			const finalization::FinalizationConfiguration& config);

	/// Creates a finalization message aggregator that attempts to reach consensus on a single value given \a config.
	std::unique_ptr<SingleStepFinalizationMessageAggregator> CreateFinalizationMessageCountVotesAggregator(
			const finalization::FinalizationConfiguration& config);

	/// Creates a finalization message aggregator that attempts to reach consensus on a block hash given \a config and \a hashes
	/// starting at \a height.
	std::unique_ptr<SingleStepFinalizationMessageAggregator> CreateFinalizationMessageCommonBlockAggregator(
			const finalization::FinalizationConfiguration& config,
			const std::vector<Hash256>& hashes,
			Height height);
}}
