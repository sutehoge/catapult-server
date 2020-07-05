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
#include "finalization/src/FinalizationConfiguration.h"
#include "finalization/src/model/FinalizationMessage.h"
#include "catapult/model/HeightHashPair.h"
#include "catapult/utils/ArraySet.h"

namespace catapult { namespace chain {

	// region BasicFinalizationMessageAggregator

	namespace {
		enum class HashConstraint { Single, Multiple };

		class BasicFinalizationMessageAggregator : public SingleStepFinalizationMessageAggregator {
		public:
			BasicFinalizationMessageAggregator(const finalization::FinalizationConfiguration& config, HashConstraint hashConstraint)
					: m_config(config)
					, m_hashConstraint(hashConstraint)
					, m_hasConsensus(false)
			{}

		public:
			bool hasConsensus() const override final {
				return m_hasConsensus;
			}

			Height consensusHeight() const override final {
				return m_consensusHeightHashPair.Height;
			}

			Hash256 consensusHash() const override final {
				return m_consensusHeightHashPair.Hash;
			}

		public:
			void reduce(FinalizationProof&) override {
				// by default, don't reduce and preserve all messages
			}

			void add(const model::FinalizationMessage& message, uint64_t numVotes) override final {
				if (!checkHashesCount(message.HashesCount)) {
					CATAPULT_LOG(debug)
							<< "skipping message for " << message.StepIdentifier
							<< " with unexpected number of hashes " << message.HashesCount;
					return;
				}

				model::HeightHashPair heightHashPair{ message.Height, *message.HashesPtr() };
				add(m_config, message.Signature.Root.ParentPublicKey, heightHashPair, numVotes);
			}

		protected:
			void setConsensus(const model::HeightHashPair& heightHashPair) {
				m_consensusHeightHashPair = heightHashPair;
				m_hasConsensus = true;
			}

		private:
			bool checkHashesCount(uint32_t count) const {
				if (0 == count)
					return false;

				if (HashConstraint::Single == m_hashConstraint)
					return 1 == count;

				return count <= m_config.MaxHashesPerPoint;
			}

		private:
			virtual void add(
					const finalization::FinalizationConfiguration& config,
					const Key& votingPublicKey,
					const model::HeightHashPair& heightHashPair,
					uint64_t numVotes) = 0;

		private:
			finalization::FinalizationConfiguration m_config;
			HashConstraint m_hashConstraint;
			bool m_hasConsensus;
			model::HeightHashPair m_consensusHeightHashPair;
		};
	}

	// endregion

	// region FinalizationMessageMaximumVotesAggregator

	namespace {
		class FinalizationMessageMaximumVotesAggregator : public BasicFinalizationMessageAggregator {
		public:
			explicit FinalizationMessageMaximumVotesAggregator(const finalization::FinalizationConfiguration& config)
					: BasicFinalizationMessageAggregator(config, HashConstraint::Multiple)
					, m_maxVotes(0)
			{}

		public:
			void reduce(FinalizationProof& proof) override {
				auto iter = std::find_if(proof.cbegin(), proof.cend(), [&searchPublicKey = m_bestVotingPublicKey](const auto& pMessage) {
					return searchPublicKey == pMessage->Signature.Root.ParentPublicKey;
				});

				auto pMessage = proof.cend() != iter ? *iter : FinalizationProof::value_type();
				proof.clear();

				if (pMessage)
					proof.push_back(pMessage);
			}

			void add(
					const finalization::FinalizationConfiguration&,
					const Key& votingPublicKey,
					const model::HeightHashPair& heightHashPair,
					uint64_t numVotes) override {
				if (numVotes <= m_maxVotes)
					return;

				m_maxVotes = numVotes;
				m_bestVotingPublicKey = votingPublicKey;
				setConsensus(heightHashPair);
			}

		private:
			uint64_t m_maxVotes;
			Key m_bestVotingPublicKey;
		};
	}

	std::unique_ptr<SingleStepFinalizationMessageAggregator> CreateFinalizationMessageMaximumVotesAggregator(
			const finalization::FinalizationConfiguration& config) {
		return std::make_unique<FinalizationMessageMaximumVotesAggregator>(config);
	}

	// endregion

	// region FinalizationMessageCountVotesAggregator

	namespace {
		struct HeightHashPairHasher {
			size_t operator()(const model::HeightHashPair& heightHashPair) const {
				return utils::ArrayHasher<Hash256>()(heightHashPair.Hash);
			}
		};

		class FinalizationMessageCountVotesAggregator : public BasicFinalizationMessageAggregator {
		public:
			explicit FinalizationMessageCountVotesAggregator(const finalization::FinalizationConfiguration& config)
					: BasicFinalizationMessageAggregator(config, HashConstraint::Single)
			{}

		public:
			void add(
					const finalization::FinalizationConfiguration& config,
					const Key& votingPublicKey,
					const model::HeightHashPair& heightHashPair,
					uint64_t numVotes) override {
				if (hasConsensus() || !m_votingPublicKeys.insert(&votingPublicKey).second)
					return;

				auto voteMapIter = m_voteMap.emplace(heightHashPair, 0).first;
				voteMapIter->second += numVotes;

				if (voteMapIter->second >= config.Threshold)
					setConsensus(heightHashPair);
			}

		private:
			std::unordered_map<model::HeightHashPair, uint64_t, HeightHashPairHasher> m_voteMap;
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
					const std::vector<Hash256>& hashes,
					Height height)
					: BasicFinalizationMessageAggregator(config, HashConstraint::Single)
					, m_hashes(hashes)
					, m_height(height)
					, m_hashVotes(m_hashes.size(), 0)
					, m_consensusHashIndex(0)
			{}

		public:
			void add(
					const finalization::FinalizationConfiguration& config,
					const Key& votingPublicKey,
					const model::HeightHashPair& heightHashPair,
					uint64_t numVotes) override {
				auto hashIndex = findIndex(heightHashPair);
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
			size_t findIndex(const model::HeightHashPair& heightHashPair) const {
				auto iter = std::find(m_hashes.cbegin(), m_hashes.cend(), heightHashPair.Hash);
				if (m_hashes.cend() == iter)
					return Not_Found;

				auto index = static_cast<size_t>(std::distance(m_hashes.cbegin(), iter));
				return m_height + Height(index) == heightHashPair.Height ? index : Not_Found;
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
						setConsensus({ m_height + Height(hashIndex), m_hashes[hashIndex] });
						m_consensusHashIndex = hashIndex;
						return;
					}
				}
			}

		private:
			std::vector<Hash256> m_hashes;
			Height m_height;

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
			const std::vector<Hash256>& hashes,
			Height height) {
		return std::make_unique<FinalizationMessageCommonBlockAggregator>(config, hashes, height);
	}

	// endregion
}}
