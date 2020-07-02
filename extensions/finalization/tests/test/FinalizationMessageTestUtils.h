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
#include "finalization/src/model/FinalizationMessage.h"
#include "catapult/crypto/KeyPair.h"
#include <memory>

namespace catapult {
	namespace cache { class AccountStateCacheDelta; }
	namespace test { struct AccountKeyPairDescriptor; }
}

namespace catapult { namespace test {

	// region message factories

	/// Creates a finalization message around one \a hash.
	std::unique_ptr<model::FinalizationMessage> CreateMessage(const Hash256& hash);

	/// Creates a finalization message around one \a hash at \a height
	std::unique_ptr<model::FinalizationMessage> CreateMessage(Height height, const Hash256& hash);

	/// Creates a finalization message with \a stepIdentifier and one \a hash.
	std::unique_ptr<model::FinalizationMessage> CreateMessage(const crypto::StepIdentifier& stepIdentifier, const Hash256& hash);

	/// Creates a valid finalization message with \a stepIdentifier and one \a hash at \a height for the account
	/// specified by \a keyPairDescriptor.
	/// \note This function assumes that the nemesis block is the last finalized block.
	std::unique_ptr<model::FinalizationMessage> CreateValidMessage(
			const crypto::StepIdentifier& stepIdentifier,
			Height height,
			const Hash256& hash,
			const AccountKeyPairDescriptor& keyPairDescriptor);

	// endregion

	// region message utils

	/// Sets the sortition hash proof in \a message given \a vrfKeyPair and \a generationHash.
	void SetMessageSortitionHashProof(
			model::FinalizationMessage& message,
			const crypto::KeyPair& vrfKeyPair,
			const GenerationHash& generationHash);

	/// Signs \a message with \a votingKeyPair.
	void SignMessage(model::FinalizationMessage& message, const crypto::KeyPair& votingKeyPair);

	// endregion

	// region account state cache utils

	/// Account descriptor that contains VRF and voting key pairs.
	struct AccountKeyPairDescriptor {
	public:
		/// Creates a descriptor around \a vrfKeyPair and \a votingKeyPair.
		AccountKeyPairDescriptor(crypto::KeyPair&& vrfKeyPair, crypto::KeyPair&& votingKeyPair)
				: VrfKeyPair(std::move(vrfKeyPair))
				, VotingKeyPair(std::move(votingKeyPair))
				, VrfPublicKey(VrfKeyPair.publicKey())
				, VotingPublicKey(VotingKeyPair.publicKey().copyTo<VotingKey>())
		{}

	public:
		/// VRF key pair.
		crypto::KeyPair VrfKeyPair;

		/// Voting key pair.
		crypto::KeyPair VotingKeyPair;

		/// VRF public key.
		Key VrfPublicKey;

		/// Voting public key.
		VotingKey VotingPublicKey;
	};

	/// Adds accounts with specified \a balances of \a mosaicId to \a accountStateCacheDelta at \a height.
	std::vector<AccountKeyPairDescriptor> AddAccountsWithBalances(
			cache::AccountStateCacheDelta& accountStateCacheDelta,
			Height height,
			MosaicId mosaicId,
			const std::vector<Amount>& balances);

	// endregion
}}