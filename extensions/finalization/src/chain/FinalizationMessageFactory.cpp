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

#include "FinalizationMessageFactory.h"
#include "finalization/src/io/ProofStorageCache.h"
#include "catapult/io/BlockStorageCache.h"

namespace catapult { namespace chain {

	namespace {
		uint64_t Clamp(uint64_t value, uint16_t multiple, uint16_t adjustment) {
			return 0 == value % multiple ? value : ((value / multiple + adjustment) * multiple);
		}

		std::pair<FinalizationPoint, Height> LoadFinalizationState(const io::ProofStorageCache& proofStorage) {
			auto view = proofStorage.view();
			return std::make_pair(view.finalizationPoint(), view.finalizedHeight());
		}

		Hash256 LoadLastFinalizedHash(const io::ProofStorageCache& proofStorage) {
			auto view = proofStorage.view();
			return view.loadFinalizedHashesFrom(view.finalizationPoint() - FinalizationPoint(1), 1).cbegin()->Hash;
		}

		model::HashRange LoadPrevoteHashChain(
				const finalization::FinalizationConfiguration& config,
				Height startHeight,
				const io::BlockStorageCache& blockStorage) {
			auto view = blockStorage.view();
			auto clampedChainHeight = Height(Clamp(view.chainHeight().unwrap(), config.PrevoteBlocksMultiple, 0));

			auto numHashes = clampedChainHeight > startHeight
					? (clampedChainHeight - startHeight).unwrap() + 1
					: 1;

			if (numHashes > config.MaxHashesPerPoint)
				numHashes -= Clamp(numHashes - config.MaxHashesPerPoint, config.PrevoteBlocksMultiple, 1);

			return view.loadHashesFrom(startHeight, numHashes);
		}

		model::HashRange ToHashRange(const Hash256& hash) {
			return model::HashRange::CopyFixed(reinterpret_cast<const uint8_t*>(&hash), 1);
		}
	}

	FinalizationMessageFactory::FinalizationMessageFactory(
			const finalization::FinalizationConfiguration& config,
			const io::BlockStorageCache& blockStorage,
			const io::ProofStorageCache& proofStorage,
			crypto::OtsTree&& otsTree)
			: m_config(config)
			, m_blockStorage(blockStorage)
			, m_proofStorage(proofStorage)
			, m_otsTree(std::move(otsTree))
	{}

	std::unique_ptr<model::FinalizationMessage> FinalizationMessageFactory::createPrevote() {
		auto finalizationState = LoadFinalizationState(m_proofStorage);
		auto hashRange = LoadPrevoteHashChain(m_config, finalizationState.second, m_blockStorage);
		if (hashRange.empty())
			hashRange = ToHashRange(LoadLastFinalizedHash(m_proofStorage));

		auto stepIdentifier = crypto::StepIdentifier{ finalizationState.first.unwrap() + 1, 1, 1 };
		return model::PrepareMessage(m_otsTree, stepIdentifier, finalizationState.second, hashRange);
	}

	std::unique_ptr<model::FinalizationMessage> FinalizationMessageFactory::createPrecommit(Height height, const Hash256& hash) {
		auto finalizationState = LoadFinalizationState(m_proofStorage);
		auto hashRange = ToHashRange(hash);

		auto stepIdentifier = crypto::StepIdentifier{ finalizationState.first.unwrap() + 1, 2, 1 };
		return model::PrepareMessage(m_otsTree, stepIdentifier, height, hashRange);
	}
}}