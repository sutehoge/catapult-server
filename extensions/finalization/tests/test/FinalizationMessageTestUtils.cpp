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

#include "FinalizationMessageTestUtils.h"
#include "catapult/cache_core/AccountStateCache.h"
#include "catapult/crypto_voting/OtsTree.h"
#include "catapult/model/BlockUtils.h"
#include "tests/test/cache/AccountStateCacheTestUtils.h"
#include "tests/test/core/mocks/MockMemoryStream.h"
#include "tests/test/nodeps/KeyTestUtils.h"
#include "tests/test/nodeps/Nemesis.h"
#include "tests/test/nodeps/Random.h"

namespace catapult { namespace test {

	// region message factories

	std::unique_ptr<model::FinalizationMessage> CreateMessage(const Hash256& hash) {
		return CreateMessage({ Random(), Random(), Random() }, hash);
	}

	std::unique_ptr<model::FinalizationMessage> CreateMessage(const crypto::StepIdentifier& stepIdentifier, const Hash256& hash) {
		uint32_t messageSize = sizeof(model::FinalizationMessage) + Hash256::Size;
		auto pMessage = utils::MakeUniqueWithSize<model::FinalizationMessage>(messageSize);
		pMessage->Size = messageSize;
		pMessage->HashesCount = 1;
		pMessage->StepIdentifier = stepIdentifier;

		FillWithRandomData(pMessage->Signature.Root.ParentPublicKey);
		*pMessage->HashesPtr() = hash;
		return pMessage;
	}

	std::unique_ptr<model::FinalizationMessage> CreateValidMessage(
			const crypto::StepIdentifier& stepIdentifier,
			const Hash256& hash,
			const AccountKeyPairDescriptor& keyPairDescriptor) {
		auto pMessage = CreateMessage(stepIdentifier, hash);
		auto nemesisGenerationHash = model::CalculateGenerationHash(GetNemesisBlock().GenerationHashProof.Gamma);

		SetMessageSortitionHashProof(*pMessage, keyPairDescriptor.VrfKeyPair, nemesisGenerationHash);
		SignMessage(*pMessage, keyPairDescriptor.VotingKeyPair);
		return pMessage;
	}

	// endregion

	// region message utils

	void SetMessageSortitionHashProof(
			model::FinalizationMessage& message,
			const crypto::KeyPair& vrfKeyPair,
			const GenerationHash& generationHash) {
		std::vector<uint8_t> sortitionVrfInputBuffer(sizeof(crypto::StepIdentifier) + GenerationHash::Size);
		std::memcpy(&sortitionVrfInputBuffer[0], &generationHash, GenerationHash::Size);
		std::memcpy(&sortitionVrfInputBuffer[GenerationHash::Size], &message.StepIdentifier, sizeof(crypto::StepIdentifier));

		message.SortitionHashProof = crypto::GenerateVrfProof(sortitionVrfInputBuffer, vrfKeyPair);
	}

	void SignMessage(model::FinalizationMessage& message, const crypto::KeyPair& votingKeyPair) {
		auto storage = mocks::MockSeekableMemoryStream();
		auto otsTree = crypto::OtsTree::Create(
				CopyKeyPair(votingKeyPair),
				storage,
				FinalizationPoint(1),
				FinalizationPoint(20),
				{ 20, 20 });
		message.Signature = otsTree.sign(message.StepIdentifier, {
			reinterpret_cast<const uint8_t*>(&message) + model::FinalizationMessage::Header_Size,
			message.Size - model::FinalizationMessage::Header_Size
		});
	}

	// endregion

	// region account state cache utils

	std::vector<AccountKeyPairDescriptor> AddAccountsWithBalances(
			cache::AccountStateCacheDelta& accountStateCacheDelta,
			Height height,
			MosaicId mosaicId,
			const std::vector<Amount>& balances) {
		std::vector<AccountKeyPairDescriptor> keyPairDescriptors;
		for (auto balance : balances) {
			keyPairDescriptors.emplace_back(GenerateKeyPair(), GenerateKeyPair());

			auto address = GenerateRandomByteArray<Address>();
			accountStateCacheDelta.addAccount(address, height);
			auto& accountState = accountStateCacheDelta.find(address).get();
			accountState.SupplementalPublicKeys.vrf().set(keyPairDescriptors.back().VrfPublicKey);
			accountState.SupplementalPublicKeys.voting().add({
				keyPairDescriptors.back().VotingPublicKey,
				FinalizationPoint(1),
				FinalizationPoint(100)
			});
			accountState.Balances.credit(mosaicId, balance);
		}

		accountStateCacheDelta.updateHighValueAccounts(height);
		return keyPairDescriptors;
	}

	// endregion
}}
