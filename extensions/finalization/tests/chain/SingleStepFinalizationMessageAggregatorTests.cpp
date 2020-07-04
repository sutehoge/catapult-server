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

#include "finalization/src/chain/SingleStepFinalizationMessageAggregator.h"
#include "finalization/src/FinalizationConfiguration.h"
#include "finalization/src/model/FinalizationMessage.h"
#include "finalization/tests/test/FinalizationMessageTestUtils.h"
#include "tests/TestHarness.h"

namespace catapult { namespace chain {

#define TEST_CLASS SingleStepFinalizationMessageAggregatorTests

	namespace {
		// region test utils

		finalization::FinalizationConfiguration CreateConfiguration(uint32_t threshold, uint32_t size) {
			auto config = finalization::FinalizationConfiguration::Uninitialized();
			config.Size = size;
			config.Threshold = threshold;
			return config;
		}

		std::unique_ptr<model::FinalizationMessage> CreateMessage(Height height, const Hash256& hash) {
			return test::CreateMessage(height, hash);
		}

		void AssertNoConsensus(const SingleStepFinalizationMessageAggregator& aggregator, const std::string& message = "") {
			EXPECT_FALSE(aggregator.hasConsensus()) << message;
			EXPECT_EQ(Height(), aggregator.consensusHeight()) << message;
			EXPECT_EQ(Hash256(), aggregator.consensusHash()) << message;
		}

		// endregion
	}

	// region constructor (MaximumVotes)

#define MAXIMUM_VOTES_TEST(TEST_NAME) TEST(TEST_CLASS, TEST_NAME##_MaximumVotes)

	MAXIMUM_VOTES_TEST(InitiallyNoConsensusIsPresent) {
		// Arrange:
		auto config = CreateConfiguration(2000, 3000);

		// Act:
		auto pAggregator = CreateFinalizationMessageMaximumVotesAggregator(config);

		// Assert:
		AssertNoConsensus(*pAggregator);
	}

	// endregion

	// region add (MaximumVotes)

	MAXIMUM_VOTES_TEST(ConsensusIsReachedAfterAnyMessage) {
		// Arrange:
		auto config = CreateConfiguration(2000, 3000);
		auto pAggregator = CreateFinalizationMessageMaximumVotesAggregator(config);

		auto hashes = test::GenerateRandomDataVector<Hash256>(3);
		auto pMessage = CreateMessage(Height(102), hashes[1]);

		// Act:
		pAggregator->add(*pMessage, 1);

		// Assert:
		EXPECT_TRUE(pAggregator->hasConsensus());
		EXPECT_EQ(Height(102), pAggregator->consensusHeight());
		EXPECT_EQ(hashes[1], pAggregator->consensusHash());
	}

	namespace {
		void AssertMaximumVotesConsensus(uint64_t numVotes1, uint64_t numVotes2, size_t expectedHashIndex) {
			// Arrange:
			auto config = CreateConfiguration(2000, 3000);
			auto pAggregator = CreateFinalizationMessageMaximumVotesAggregator(config);

			auto hashes = test::GenerateRandomDataVector<Hash256>(3);
			auto pMessage1 = CreateMessage(Height(102), hashes[0]);
			auto pMessage2 = CreateMessage(Height(104), hashes[2]);

			// Act:
			pAggregator->add(*pMessage1, numVotes1);
			pAggregator->add(*pMessage2, numVotes2);

			// Assert:
			std::ostringstream description;
			description << "numVotes1 = " << numVotes1 << ", numVotes2 = " << numVotes2;

			EXPECT_TRUE(pAggregator->hasConsensus()) << description.str();
			EXPECT_EQ(Height(102 + expectedHashIndex), pAggregator->consensusHeight()) << description.str();
			EXPECT_EQ(hashes[expectedHashIndex], pAggregator->consensusHash()) << description.str();
		}
	}

	MAXIMUM_VOTES_TEST(ConsensusIsNotUpdatedWhenMessageWithLessVotesIsAdded) {
		AssertMaximumVotesConsensus(1000, 999, 0);
		AssertMaximumVotesConsensus(1000, 1, 0);
	}

	MAXIMUM_VOTES_TEST(ConsensusIsNotUpdatedWhenMessageWithEqualVotesIsAdded) {
		AssertMaximumVotesConsensus(1000, 1000, 0);
	}

	MAXIMUM_VOTES_TEST(ConsensusIsUpdatedWhenMessageWithMoreVotesIsAdded) {
		AssertMaximumVotesConsensus(1000, 1001, 2);
		AssertMaximumVotesConsensus(1000, 9999, 2);
	}

	// endregion

	// region reduce (MaximumVotes)

	MAXIMUM_VOTES_TEST(ReduceOnlyPreservesBestMessageWhenBestMessageIsPresent) {
		// Arrange:
		auto config = CreateConfiguration(2000, 3000);
		auto pAggregator = CreateFinalizationMessageMaximumVotesAggregator(config);

		auto hashes = test::GenerateRandomDataVector<Hash256>(3);
		auto pMessage1 = utils::UniqueToShared(CreateMessage(Height(102), hashes[0]));
		auto pMessage2 = utils::UniqueToShared(CreateMessage(Height(103), hashes[1]));
		auto pMessage3 = utils::UniqueToShared(CreateMessage(Height(104), hashes[2]));

		FinalizationProof proof;
		proof.push_back(pMessage1);
		proof.push_back(pMessage2);
		proof.push_back(pMessage3);

		// Act:
		pAggregator->add(*pMessage1, 100);
		pAggregator->add(*pMessage2, 300);
		pAggregator->add(*pMessage3, 200);
		pAggregator->reduce(proof);

		// Assert:
		ASSERT_EQ(1u, proof.size());
		test::AssertEqualMessage(*pMessage2, *proof[0], "best message");
	}

	MAXIMUM_VOTES_TEST(ReducePreservesNoMessagesWhenBestMessageIsNotPresent) {
		// Arrange:
		auto config = CreateConfiguration(2000, 3000);
		auto pAggregator = CreateFinalizationMessageMaximumVotesAggregator(config);

		auto hashes = test::GenerateRandomDataVector<Hash256>(3);
		auto pMessage1 = utils::UniqueToShared(CreateMessage(Height(102), hashes[0]));
		auto pMessage2 = utils::UniqueToShared(CreateMessage(Height(103), hashes[1]));
		auto pMessage3 = utils::UniqueToShared(CreateMessage(Height(104), hashes[2]));

		FinalizationProof proof;
		proof.push_back(pMessage1);
		proof.push_back(pMessage3);

		// Act:
		pAggregator->add(*pMessage1, 100);
		pAggregator->add(*pMessage2, 300);
		pAggregator->add(*pMessage3, 200);
		pAggregator->reduce(proof);

		// Assert:
		EXPECT_TRUE(proof.empty());
	}

	// endregion

	// region traits (CountVotes + CommonBlock)

	namespace {
		struct CountVotesTraits {
			static auto CreateFinalizationMessageAggregator(
					const finalization::FinalizationConfiguration& config,
					const std::vector<Hash256>&,
					Height) {
				return CreateFinalizationMessageCountVotesAggregator(config);
			}
		};

		struct CommonBlockTraits {
			static constexpr auto CreateFinalizationMessageAggregator = CreateFinalizationMessageCommonBlockAggregator;
		};
	}

#define AGGREGATOR_TEST(TEST_NAME) \
	template<typename TTraits> void TRAITS_TEST_NAME(TEST_CLASS, TEST_NAME)(); \
	TEST(TEST_CLASS, TEST_NAME##_CountVotes) { TRAITS_TEST_NAME(TEST_CLASS, TEST_NAME)<CountVotesTraits>(); } \
	TEST(TEST_CLASS, TEST_NAME##_CommonBlock) { TRAITS_TEST_NAME(TEST_CLASS, TEST_NAME)<CommonBlockTraits>(); } \
	template<typename TTraits> void TRAITS_TEST_NAME(TEST_CLASS, TEST_NAME)()

	// endregion

	// region constructor (CountVotes + CommonBlock)

	AGGREGATOR_TEST(InitiallyNoConsensusIsPresent) {
		// Arrange:
		auto config = CreateConfiguration(2000, 3000);

		// Act:
		auto pAggregator = TTraits::CreateFinalizationMessageAggregator(config, test::GenerateRandomDataVector<Hash256>(3), Height(101));

		// Assert:
		AssertNoConsensus(*pAggregator);
	}

	// endregion

	// region single message (CountVotes + CommonBlock)

	namespace {
		template<typename TTraits>
		void AssertMessageWithVotesDoesNotReachConsensus(uint32_t numVotes) {
			// Arrange:
			auto config = CreateConfiguration(2000, 3000);
			auto hashes = test::GenerateRandomDataVector<Hash256>(3);
			auto pAggregator = TTraits::CreateFinalizationMessageAggregator(config, hashes, Height(101));

			auto pMessage = CreateMessage(Height(102), hashes[1]);

			// Act:
			pAggregator->add(*pMessage, numVotes);

			// Assert:
			AssertNoConsensus(*pAggregator, std::to_string(numVotes));
		}

		template<typename TTraits>
		void AssertMessageWithVotesReachesConsensus(uint32_t numVotes) {
			// Arrange:
			auto config = CreateConfiguration(2000, 3000);
			auto hashes = test::GenerateRandomDataVector<Hash256>(3);
			auto pAggregator = TTraits::CreateFinalizationMessageAggregator(config, hashes, Height(101));

			auto pMessage = CreateMessage(Height(102), hashes[1]);

			// Act:
			pAggregator->add(*pMessage, numVotes);

			// Assert:
			EXPECT_TRUE(pAggregator->hasConsensus()) << numVotes;
			EXPECT_EQ(Height(102), pAggregator->consensusHeight()) << numVotes;
			EXPECT_EQ(hashes[1], pAggregator->consensusHash()) << numVotes;
		}
	}

	AGGREGATOR_TEST(MessageWithLessThanThresholdVotesDoesNotReachConsensus) {
		AssertMessageWithVotesDoesNotReachConsensus<TTraits>(0);
		AssertMessageWithVotesDoesNotReachConsensus<TTraits>(1);
		AssertMessageWithVotesDoesNotReachConsensus<TTraits>(1000);
		AssertMessageWithVotesDoesNotReachConsensus<TTraits>(1999);
	}

	AGGREGATOR_TEST(MessageWithExactlyThresholdVotesReachesConsensus) {
		AssertMessageWithVotesReachesConsensus<TTraits>(2000);
	}

	AGGREGATOR_TEST(MessageWithGreaterThanThresholdVotesReachesConsensus) {
		AssertMessageWithVotesReachesConsensus<TTraits>(2001);
		AssertMessageWithVotesReachesConsensus<TTraits>(2500);
		AssertMessageWithVotesReachesConsensus<TTraits>(3000);
	}

	// endregion

	// region multiple messages (CountVotes + CommonBlock)

	AGGREGATOR_TEST(MessageVotesAreAdditiveWhenBothHeightAndHashMatch) {
		// Arrange:
		auto config = CreateConfiguration(2000, 3000);
		auto hashes = test::GenerateRandomDataVector<Hash256>(3);
		auto pAggregator = TTraits::CreateFinalizationMessageAggregator(config, hashes, Height(101));

		auto pMessage1 = CreateMessage(Height(102), hashes[1]);
		auto pMessage2 = CreateMessage(Height(102), hashes[1]);

		pAggregator->add(*pMessage1, 1100);

		// Sanity:
		EXPECT_FALSE(pAggregator->hasConsensus());

		// Act:
		pAggregator->add(*pMessage2, 1000);

		// Assert: 2100 > 2000
		EXPECT_TRUE(pAggregator->hasConsensus());
		EXPECT_EQ(Height(102), pAggregator->consensusHeight());
		EXPECT_EQ(hashes[1], pAggregator->consensusHash());
	}

	AGGREGATOR_TEST(MessageVotesAreNotAdditiveWhenOnlyHeightMatchs) {
		// Arrange:
		auto config = CreateConfiguration(2000, 3000);
		auto hashes = test::GenerateRandomDataVector<Hash256>(3);
		auto pAggregator = TTraits::CreateFinalizationMessageAggregator(config, hashes, Height(101));

		auto pMessage1 = CreateMessage(Height(102), hashes[0]);
		auto pMessage2 = CreateMessage(Height(102), hashes[1]);

		pAggregator->add(*pMessage1, 1100);

		// Act:
		pAggregator->add(*pMessage2, 1000);

		// Assert:
		AssertNoConsensus(*pAggregator);
	}

	AGGREGATOR_TEST(MessageVotesAreNotAdditiveWhenOnlyHashMatchs) {
		// Arrange:
		auto config = CreateConfiguration(2000, 3000);
		auto hashes = test::GenerateRandomDataVector<Hash256>(3);
		auto pAggregator = TTraits::CreateFinalizationMessageAggregator(config, hashes, Height(101));

		auto pMessage1 = CreateMessage(Height(101), hashes[1]);
		auto pMessage2 = CreateMessage(Height(102), hashes[1]);

		pAggregator->add(*pMessage1, 1100);

		// Act:
		pAggregator->add(*pMessage2, 1000);

		// Assert:
		AssertNoConsensus(*pAggregator);
	}

	AGGREGATOR_TEST(RedundantVotesAreIgnored) {
		// Arrange:
		auto config = CreateConfiguration(2000, 3000);
		auto hashes = test::GenerateRandomDataVector<Hash256>(3);
		auto pAggregator = TTraits::CreateFinalizationMessageAggregator(config, hashes, Height(101));

		auto pMessage1 = CreateMessage(Height(102), hashes[1]);
		auto pMessage2 = CreateMessage(Height(102), hashes[1]);
		pMessage2->Signature.Root.ParentPublicKey = pMessage1->Signature.Root.ParentPublicKey;

		// Act:
		pAggregator->add(*pMessage1, 1100);
		pAggregator->add(*pMessage2, 1000);

		// Assert:
		AssertNoConsensus(*pAggregator);
	}

	AGGREGATOR_TEST(MessageVotersCannotVoteForConflictingHashes) {
		// Arrange:
		auto config = CreateConfiguration(2000, 3000);
		auto hashes = test::GenerateRandomDataVector<Hash256>(3);
		auto pAggregator = TTraits::CreateFinalizationMessageAggregator(config, hashes, Height(101));

		auto pMessage1 = CreateMessage(Height(102), test::GenerateRandomByteArray<Hash256>());
		auto pMessage2 = CreateMessage(Height(102), hashes[1]);
		pMessage2->Signature.Root.ParentPublicKey = pMessage1->Signature.Root.ParentPublicKey;

		// Act:
		pAggregator->add(*pMessage1, 1100);
		pAggregator->add(*pMessage2, 2500);

		// Assert: second message is ignored (voter is malicious)
		AssertNoConsensus(*pAggregator);
	}

	// endregion

	// region reduce (CountVotes + CommonBlock)

	AGGREGATOR_TEST(ReduceDoesNotChangeProof) {
		// Arrange:
		auto config = CreateConfiguration(2000, 3000);
		auto hashes = test::GenerateRandomDataVector<Hash256>(3);
		auto pAggregator = TTraits::CreateFinalizationMessageAggregator(config, hashes, Height(101));

		auto pMessage1 = utils::UniqueToShared(CreateMessage(Height(102), test::GenerateRandomByteArray<Hash256>()));
		auto pMessage2 = utils::UniqueToShared(CreateMessage(Height(102), hashes[1]));

		FinalizationProof proof;
		proof.push_back(pMessage1);
		proof.push_back(pMessage2);

		// Act:
		pAggregator->reduce(proof);

		// Assert:
		ASSERT_EQ(2u, proof.size());
		test::AssertEqualMessage(*pMessage1, *proof[0], "message 1");
		test::AssertEqualMessage(*pMessage2, *proof[1], "message 2");
	}

	// endregion

	// region multiple messages (CountVotes)

	TEST(TEST_CLASS, ConsensusCannotBeChangedAfterItIsReached_CountVotes) {
		// Arrange:
		auto config = CreateConfiguration(2000, 3000);
		auto pAggregator = CreateFinalizationMessageCountVotesAggregator(config);

		auto hash = test::GenerateRandomByteArray<Hash256>();
		auto pMessage1 = CreateMessage(Height(102), hash);
		auto pMessage2 = CreateMessage(Height(103), test::GenerateRandomByteArray<Hash256>());

		// Act:
		pAggregator->add(*pMessage1, 2100);
		pAggregator->add(*pMessage2, 2500);

		// Assert:
		EXPECT_TRUE(pAggregator->hasConsensus());
		EXPECT_EQ(Height(102), pAggregator->consensusHeight());
		EXPECT_EQ(hash, pAggregator->consensusHash());
	}

	// endregion

	// region multiple messages (CommonBlock)

	namespace {
		void AssertConsensusCannotBeChangedToEarlierHash(bool shouldReuseVoter, size_t delta) {
			// Arrange:
			auto config = CreateConfiguration(2000, 3000);
			auto hashes = test::GenerateRandomDataVector<Hash256>(3 + delta);
			auto pAggregator = CreateFinalizationMessageCommonBlockAggregator(config, hashes, Height(101));

			auto pMessage1 = CreateMessage(Height(101 + 2 + delta), hashes[2 + delta]);
			auto pMessage2 = CreateMessage(Height(101 + 1 + delta), hashes[1 + delta]);
			auto pMessage3 = CreateMessage(Height(102), hashes[1]);

			if (shouldReuseVoter)
				pMessage3->Signature.Root.ParentPublicKey = pMessage2->Signature.Root.ParentPublicKey;

			// Act:
			pAggregator->add(*pMessage1, 1000);
			pAggregator->add(*pMessage2, 1100); // hashes [0 .. `1 + delta`] should exceed threshold
			pAggregator->add(*pMessage3, shouldReuseVoter ? 1100 : 1200); // hashes [0 .. `1 + delta`] should exceed threshold

			// Assert:
			EXPECT_TRUE(pAggregator->hasConsensus());
			EXPECT_EQ(Height(101 + 1 + delta), pAggregator->consensusHeight());
			EXPECT_EQ(hashes[1 + delta], pAggregator->consensusHash());
		}
	}

	TEST(TEST_CLASS, ConsensusCannotBeChangedToEarlierHash_CommonBlock) {
		AssertConsensusCannotBeChangedToEarlierHash(false, 0);
		AssertConsensusCannotBeChangedToEarlierHash(false, 10);
	}

	TEST(TEST_CLASS, ConsensusCannotBeChangedToEarlierHashSameVoter_CommonBlock) {
		AssertConsensusCannotBeChangedToEarlierHash(true, 0);
		AssertConsensusCannotBeChangedToEarlierHash(true, 10);
	}

	namespace {
		void AssertConsensusCanBeChangedToLaterHash(bool shouldReuseVoter, size_t delta) {
			// Arrange:
			auto config = CreateConfiguration(2000, 3000);
			auto hashes = test::GenerateRandomDataVector<Hash256>(3 + delta);
			auto pAggregator = CreateFinalizationMessageCommonBlockAggregator(config, hashes, Height(101));

			auto pMessage1 = CreateMessage(Height(101 + 1 + delta), hashes[1 + delta]);
			auto pMessage2 = CreateMessage(Height(102), hashes[1]);
			auto pMessage3 = CreateMessage(Height(101 + 2 + delta), hashes[2 + delta]);

			if (shouldReuseVoter)
				pMessage3->Signature.Root.ParentPublicKey = pMessage2->Signature.Root.ParentPublicKey;

			// Act:
			pAggregator->add(*pMessage1, 1000);
			pAggregator->add(*pMessage2, 1100); // hashes [0, 1] should exceed threshold
			pAggregator->add(*pMessage3, shouldReuseVoter ? 1100 : 1200); // hashes [0 .. `1 + delta`] should exceed threshold

			// Assert:
			EXPECT_TRUE(pAggregator->hasConsensus());
			EXPECT_EQ(Height(101 + 1 + delta), pAggregator->consensusHeight());
			EXPECT_EQ(hashes[1 + delta], pAggregator->consensusHash());
		}
	}

	TEST(TEST_CLASS, ConsensusCanBeChangedToLaterHash_CommonBlock) {
		AssertConsensusCanBeChangedToLaterHash(false, 0);
		AssertConsensusCanBeChangedToLaterHash(false, 10);
	}

	TEST(TEST_CLASS, ConsensusCanBeChangedToLaterHashSameVoter_CommonBlock) {
		AssertConsensusCanBeChangedToLaterHash(true, 0);
		AssertConsensusCanBeChangedToLaterHash(true, 10);
	}

	namespace {
		void AssertCommonBlockCannotReachConsensusForHashAtUnexpectedHeight(Height height) {
			// Arrange:
			auto config = CreateConfiguration(2000, 3000);
			auto hashes = test::GenerateRandomDataVector<Hash256>(3);
			auto pAggregator = CreateFinalizationMessageCommonBlockAggregator(config, hashes, Height(101));

			auto pMessage1 = CreateMessage(height, hashes[1]);
			auto pMessage2 = CreateMessage(height, hashes[1]);

			pAggregator->add(*pMessage1, 1100);

			// Act:
			pAggregator->add(*pMessage2, 1000);

			// Assert:
			AssertNoConsensus(*pAggregator);
		}
	}

	TEST(TEST_CLASS, ConsensusCanNotBeReachedForHashAtUnexpectedHeight_CommonBlock) {
		AssertCommonBlockCannotReachConsensusForHashAtUnexpectedHeight(Height(101));
		AssertCommonBlockCannotReachConsensusForHashAtUnexpectedHeight(Height(103));
	}

	// endregion
}}
