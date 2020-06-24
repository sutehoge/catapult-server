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

#include "SingleStepFinalizationMessageAggregator.h"
#include "finalization/src/model/FinalizationMessage.h"
#include "catapult/utils/ArraySet.h"

namespace catapult { namespace chain {

	// region BasicFinalizationMessageAggregator

	namespace {
		class BasicFinalizationMessageAggregator : public SingleStepFinalizationMessageAggregator {
		public:
			explicit BasicFinalizationMessageAggregator(const finalization::FinalizationConfiguration& config)
					: m_config(config)
					, m_hasConsensus(false)
			{}

		public:
			bool hasConsensus() const override final {
				return m_hasConsensus;
			}

			Hash256 consensusHash() const override final {
				return m_consensusHash;
			}

		public:
			void add(const model::FinalizationMessage& message, uint64_t numVotes) override final {
				add(m_config, message.Signature.Root.ParentPublicKey, *message.HashesPtr(), numVotes);
			}

		protected:
			void setConsensusHash(const Hash256& hash) {
				m_consensusHash = hash;
				m_hasConsensus = true;
			}

		private:
			virtual void add(
					const finalization::FinalizationConfiguration& config,
					const Key& votingPublicKey,
					const Hash256& hash,
					uint64_t numVotes) = 0;

		private:
			finalization::FinalizationConfiguration m_config;
			bool m_hasConsensus;
			Hash256 m_consensusHash;
		};
	}

	// endregion

	// region FinalizationMessageCountVotesAggregator

	namespace {
		class FinalizationMessageCountVotesAggregator : public BasicFinalizationMessageAggregator {
		public:
			explicit FinalizationMessageCountVotesAggregator(const finalization::FinalizationConfiguration& config)
					: BasicFinalizationMessageAggregator(config)
			{}

		public:
			void add(
					const finalization::FinalizationConfiguration& config,
					const Key& votingPublicKey,
					const Hash256& hash,
					uint64_t numVotes) override {
				if (hasConsensus() || !m_votingPublicKeys.insert(&votingPublicKey).second)
					return;

				auto hashVoteMapIter = m_hashVoteMap.emplace(hash, 0).first;
				hashVoteMapIter->second += numVotes;

				if (hashVoteMapIter->second >= config.Threshold)
					setConsensusHash(hash);
			}

		private:
			std::unordered_map<Hash256, uint64_t, utils::ArrayHasher<Hash256>> m_hashVoteMap;
			utils::KeyPointerSet m_votingPublicKeys;
		};
	}

	std::unique_ptr<SingleStepFinalizationMessageAggregator> CreateFinalizationMessageCountVotesAggregator(
			const finalization::FinalizationConfiguration& config) {
		return std::make_unique<FinalizationMessageCountVotesAggregator>(config);
	}

	// endregion

	// region FinalizationMessageCommonBlockAggregator

	namespace {
		class FinalizationMessageCommonBlockAggregator : public BasicFinalizationMessageAggregator {
		private:
			static constexpr auto Not_Found = std::numeric_limits<size_t>::max();

		public:
			FinalizationMessageCommonBlockAggregator(
					const finalization::FinalizationConfiguration& config,
					const std::vector<Hash256>& hashes)
					: BasicFinalizationMessageAggregator(config)
					, m_hashes(hashes)
					, m_hashVotes(m_hashes.size(), 0)
					, m_consensusHashIndex(0)
			{}

		public:
			void add(
					const finalization::FinalizationConfiguration& config,
					const Key& votingPublicKey,
					const Hash256& hash,
					uint64_t numVotes) override {
				auto hashIndex = findIndex(hash);
				auto publicKeyHashIndexMapInsertResult = m_publicKeyHashIndexMap.emplace(&votingPublicKey, hashIndex);
				if (Not_Found == hashIndex)
					return;

				size_t firstHashIndexToIncrement = 0;
				if (!publicKeyHashIndexMapInsertResult.second) {
					auto& mapHashIndex = publicKeyHashIndexMapInsertResult.first->second;
					if (hashIndex <= mapHashIndex)
						return;

					// only increment hashes not previously incremented
					firstHashIndexToIncrement = mapHashIndex + 1;
					mapHashIndex = hashIndex;
				}

				incrementVotes(config, firstHashIndexToIncrement, hashIndex, numVotes);
			}

		private:
			size_t findIndex(const Hash256& searchHash) const {
				auto iter = std::find(m_hashes.cbegin(), m_hashes.cend(), searchHash);
				return m_hashes.cend() == iter ? Not_Found : static_cast<size_t>(std::distance(m_hashes.cbegin(), iter));
			}

			void incrementVotes(
					const finalization::FinalizationConfiguration& config,
					size_t startIndex,
					size_t endIndex,
					uint64_t numVotes) {
				// if there is already consensus, only allow new consensus that includes more hashes
				auto adjustedStartIndex = hasConsensus() ? m_consensusHashIndex + 1 : startIndex;
				for (auto i = endIndex + 1; i > adjustedStartIndex; --i) {
					auto hashIndex = i - 1;
					m_hashVotes[hashIndex] += numVotes;
					if (m_hashVotes[hashIndex] >= config.Threshold) {
						setConsensusHash(m_hashes[hashIndex]);
						m_consensusHashIndex = hashIndex;
						return;
					}
				}
			}

		private:
			std::vector<Hash256> m_hashes;
			std::vector<uint64_t> m_hashVotes;
			size_t m_consensusHashIndex;

			using PublicKeyHashIndexMap = std::unordered_map<
				const Key*,
				size_t,
				utils::ArrayPointerHasher<Key>,
				utils::ArrayPointerEquality<Key>>;
			PublicKeyHashIndexMap m_publicKeyHashIndexMap;
		};
	}

	std::unique_ptr<SingleStepFinalizationMessageAggregator> CreateFinalizationMessageCommonBlockAggregator(
			const finalization::FinalizationConfiguration& config,
			const std::vector<Hash256>& hashes) {
		return std::make_unique<FinalizationMessageCommonBlockAggregator>(config, hashes);
	}

	// endregion
}}
