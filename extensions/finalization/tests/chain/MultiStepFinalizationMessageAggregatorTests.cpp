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

#include "finalization/src/chain/MultiStepFinalizationMessageAggregator.h"
#include "finalization/src/model/FinalizationMessage.h"
#include "tests/TestHarness.h"

namespace catapult { namespace chain {

#define TEST_CLASS MultiStepFinalizationMessageAggregatorTests

	namespace {
		using ConsensusTuples = std::vector<std::pair<crypto::StepIdentifier, Hash256>>;

		// region MockFinalizationMessageAggregator

		class MockFinalizationMessageAggregator : public SingleStepFinalizationMessageAggregator {
		public:
			explicit MockFinalizationMessageAggregator(const finalization::FinalizationConfiguration& config)
					: m_config(config)
					, m_hasConsensus(false)
					, m_numVotes(0)
			{}

		public:
			bool hasConsensus() const override {
				return m_hasConsensus;
			}

			Hash256 consensusHash() const override {
				return m_consensusHash;
			}

		public:
			const auto& breadcrumbs() const {
				return m_breadcrumbs;
			}

		public:
			void add(const model::FinalizationMessage& message, uint64_t numVotes) override {
				m_breadcrumbs.emplace_back(message.Signature.Root.ParentPublicKey, numVotes);

				m_numVotes += numVotes;
				if (m_numVotes >= m_config.Threshold) {
					m_consensusHash = *message.HashesPtr();
					m_hasConsensus = true;
				}
			}

		private:
			finalization::FinalizationConfiguration m_config;
			bool m_hasConsensus;
			Hash256 m_consensusHash;

			uint64_t m_numVotes;
			std::vector<std::pair<Key, uint64_t>> m_breadcrumbs;
		};

		// endregion

		// region TestContext

		class TestContext {
		public:
			TestContext(uint32_t threshold, uint32_t size) {
				auto config = finalization::FinalizationConfiguration::Uninitialized();
				config.Size = size;
				config.Threshold = threshold;

				m_pMultiStepAggregator = std::make_unique<MultiStepFinalizationMessageAggregator>(
						config,
						createAggregatorFactory(),
						createConsensusSink());
			}

		public:
			const auto& consensusTuples() const {
				return m_consensusTuples;
			}

			const auto& singleStepAggregators() const {
				return m_singleStepAggregators;
			}

		public:
			auto& multiStepAggregator() {
				return *m_pMultiStepAggregator;
			}

		private:
			MultiStepFinalizationMessageAggregator::AggregatorFactory createAggregatorFactory() {
				return [&singleStepAggregators = m_singleStepAggregators](const auto& config) {
					auto pAggregator = std::make_unique<MockFinalizationMessageAggregator>(config);
					singleStepAggregators.push_back(pAggregator.get());
					return pAggregator;
				};
			}

			MultiStepFinalizationMessageAggregator::ConsensusSink createConsensusSink() {
				return [&consensusTuples = m_consensusTuples](const auto& stepIdentifier, const auto& hash) {
					consensusTuples.emplace_back(stepIdentifier, hash);
				};
			}

		private:
			std::unique_ptr<MultiStepFinalizationMessageAggregator> m_pMultiStepAggregator;
			std::vector<MockFinalizationMessageAggregator*> m_singleStepAggregators;
			ConsensusTuples m_consensusTuples;
		};

		// endregion

		// region test utils

		std::unique_ptr<model::FinalizationMessage> CreateMessage(const crypto::StepIdentifier& stepIdentifier, const Hash256& hash) {
			uint32_t messageSize = sizeof(model::FinalizationMessage) + Hash256::Size;
			auto pMessage = utils::MakeUniqueWithSize<model::FinalizationMessage>(messageSize);
			pMessage->Size = messageSize;
			pMessage->HashesCount = 1;
			pMessage->StepIdentifier = stepIdentifier;

			test::FillWithRandomData(pMessage->Signature.Root.ParentPublicKey);
			*pMessage->HashesPtr() = hash;
			return pMessage;
		}

		std::pair<Key, uint64_t> MakeBreadcrumb(const Key& publicKey, uint64_t numVotes) {
			return std::make_pair(publicKey, numVotes);
		}

		ConsensusTuples MakeConsensusTuples(const crypto::StepIdentifier& stepIdentifier, const std::vector<Hash256>& hashes) {
			ConsensusTuples consensusTuples;
			for (const auto& hash : hashes)
				consensusTuples.emplace_back(stepIdentifier, hash);

			return consensusTuples;
		}

		// endregion
	}

	// region constructor

	TEST(TEST_CLASS, InitiallyAggregatorIsEmpty) {
		// Arrange:
		TestContext context(2000, 3000);

		// Act:
		const auto& aggregator = context.multiStepAggregator();

		// Assert:
		EXPECT_EQ(0u, aggregator.size());
		EXPECT_TRUE(context.consensusTuples().empty());
	}

	// endregion

	// region single step

	TEST(TEST_CLASS, CanAddSingleStepMessagesThatDoNotReachConsensus) {
		// Arrange:
		TestContext context(2000, 3000);
		auto& aggregator = context.multiStepAggregator();

		auto pMessage1 = CreateMessage({ 3, 4, 5 }, test::GenerateRandomByteArray<Hash256>());
		auto pMessage2 = CreateMessage({ 3, 4, 5 }, test::GenerateRandomByteArray<Hash256>());
		auto pMessage3 = CreateMessage({ 3, 4, 5 }, test::GenerateRandomByteArray<Hash256>());

		// Act:
		aggregator.add(*pMessage1, 1000);
		aggregator.add(*pMessage2, 400);
		aggregator.add(*pMessage3, 500);

		// Assert:
		EXPECT_EQ(1u, aggregator.size());
		EXPECT_TRUE(context.consensusTuples().empty());

		// - check single step aggregator
		ASSERT_EQ(1u, context.singleStepAggregators().size());

		const auto& breadcrumbs = context.singleStepAggregators()[0]->breadcrumbs();
		ASSERT_EQ(3u, breadcrumbs.size());
		EXPECT_EQ(MakeBreadcrumb(pMessage1->Signature.Root.ParentPublicKey, 1000), breadcrumbs[0]);
		EXPECT_EQ(MakeBreadcrumb(pMessage2->Signature.Root.ParentPublicKey, 400), breadcrumbs[1]);
		EXPECT_EQ(MakeBreadcrumb(pMessage3->Signature.Root.ParentPublicKey, 500), breadcrumbs[2]);
	}

	TEST(TEST_CLASS, CanAddSingleStepMessagesThatReachConsensus) {
		// Arrange:
		TestContext context(2000, 3000);
		auto& aggregator = context.multiStepAggregator();

		auto hashes = test::GenerateRandomDataVector<Hash256>(3);
		auto pMessage1 = CreateMessage({ 3, 4, 5 }, hashes[0]);
		auto pMessage2 = CreateMessage({ 3, 4, 5 }, hashes[1]);
		auto pMessage3 = CreateMessage({ 3, 4, 5 }, hashes[2]);

		// Act:
		aggregator.add(*pMessage1, 1000);
		aggregator.add(*pMessage2, 750);
		aggregator.add(*pMessage3, 250);

		// Assert:
		EXPECT_EQ(1u, aggregator.size());
		EXPECT_EQ(MakeConsensusTuples({ 3, 4, 5 }, { hashes[2] }), context.consensusTuples());

		// - check single step aggregator
		ASSERT_EQ(1u, context.singleStepAggregators().size());

		const auto& breadcrumbs = context.singleStepAggregators()[0]->breadcrumbs();
		ASSERT_EQ(3u, breadcrumbs.size());
		EXPECT_EQ(MakeBreadcrumb(pMessage1->Signature.Root.ParentPublicKey, 1000), breadcrumbs[0]);
		EXPECT_EQ(MakeBreadcrumb(pMessage2->Signature.Root.ParentPublicKey, 750), breadcrumbs[1]);
		EXPECT_EQ(MakeBreadcrumb(pMessage3->Signature.Root.ParentPublicKey, 250), breadcrumbs[2]);
	}

	TEST(TEST_CLASS, CanAddSingleStepMessagesThatReachConsensusMultipleTimes) {
		// Arrange:
		TestContext context(2000, 3000);
		auto& aggregator = context.multiStepAggregator();

		auto hashes = test::GenerateRandomDataVector<Hash256>(2);
		auto pMessage1 = CreateMessage({ 3, 4, 5 }, hashes[0]);
		auto pMessage2 = CreateMessage({ 3, 4, 5 }, hashes[0]);
		auto pMessage3 = CreateMessage({ 3, 4, 5 }, hashes[1]);

		// Act:
		aggregator.add(*pMessage1, 2000);
		aggregator.add(*pMessage2, 1);
		aggregator.add(*pMessage3, 2);

		// Assert:
		EXPECT_EQ(1u, aggregator.size());
		EXPECT_EQ(MakeConsensusTuples({ 3, 4, 5 }, { hashes[0], hashes[0], hashes[1] }), context.consensusTuples());

		// - check single step aggregator
		ASSERT_EQ(1u, context.singleStepAggregators().size());

		const auto& breadcrumbs = context.singleStepAggregators()[0]->breadcrumbs();
		ASSERT_EQ(3u, breadcrumbs.size());
		EXPECT_EQ(MakeBreadcrumb(pMessage1->Signature.Root.ParentPublicKey, 2000), breadcrumbs[0]);
		EXPECT_EQ(MakeBreadcrumb(pMessage2->Signature.Root.ParentPublicKey, 1), breadcrumbs[1]);
		EXPECT_EQ(MakeBreadcrumb(pMessage3->Signature.Root.ParentPublicKey, 2), breadcrumbs[2]);
	}

	// endregion

	// region multiple steps

	TEST(TEST_CLASS, CanAddMultiStepMessagesThatDoNotReachConsensus) {
		// Arrange:
		TestContext context(2000, 3000);
		auto& aggregator = context.multiStepAggregator();

		auto pMessage1 = CreateMessage({ 6, 4, 5 }, test::GenerateRandomByteArray<Hash256>());
		auto pMessage2 = CreateMessage({ 8, 4, 5 }, test::GenerateRandomByteArray<Hash256>()); // greater step
		auto pMessage3 = CreateMessage({ 4, 4, 5 }, test::GenerateRandomByteArray<Hash256>()); // less step

		// Act:
		aggregator.add(*pMessage1, 1000);
		aggregator.add(*pMessage2, 400);
		aggregator.add(*pMessage3, 700);

		// Assert:
		EXPECT_EQ(3u, aggregator.size());
		EXPECT_TRUE(context.consensusTuples().empty());

		// - check single step aggregators (no consensus was reached, so no pruning should happen)
		ASSERT_EQ(3u, context.singleStepAggregators().size());

		const auto& breadcrumbs1 = context.singleStepAggregators()[0]->breadcrumbs();
		ASSERT_EQ(1u, breadcrumbs1.size());
		EXPECT_EQ(MakeBreadcrumb(pMessage1->Signature.Root.ParentPublicKey, 1000), breadcrumbs1[0]);

		const auto& breadcrumbs2 = context.singleStepAggregators()[1]->breadcrumbs();
		ASSERT_EQ(1u, breadcrumbs2.size());
		EXPECT_EQ(MakeBreadcrumb(pMessage2->Signature.Root.ParentPublicKey, 400), breadcrumbs2[0]);

		const auto& breadcrumbs3 = context.singleStepAggregators()[2]->breadcrumbs();
		ASSERT_EQ(1u, breadcrumbs3.size());
		EXPECT_EQ(MakeBreadcrumb(pMessage3->Signature.Root.ParentPublicKey, 700), breadcrumbs3[0]);
	}

	TEST(TEST_CLASS, CanAddMultiStepMessagesThatReachConsensus) {
		// Arrange:
		TestContext context(2000, 3000);
		auto& aggregator = context.multiStepAggregator();

		auto hashes = test::GenerateRandomDataVector<Hash256>(4);
		auto pMessage1 = CreateMessage({ 6, 4, 5 }, hashes[0]);
		auto pMessage2 = CreateMessage({ 8, 4, 5 }, hashes[1]); // greater step
		auto pMessage3 = CreateMessage({ 4, 4, 5 }, hashes[2]); // less step
		auto pMessage4 = CreateMessage({ 6, 4, 5 }, hashes[3]);

		// Act: reach consensus on { 6, 4, 5 }
		aggregator.add(*pMessage1, 1000);
		aggregator.add(*pMessage2, 400);
		aggregator.add(*pMessage3, 700);
		aggregator.add(*pMessage4, 1100);

		// Assert: aggregator associated with { 4, 4, 5 } is dropped
		EXPECT_EQ(2u, aggregator.size());
		EXPECT_EQ(MakeConsensusTuples({ 6, 4, 5 }, { hashes[3] }), context.consensusTuples());

		// - check single step aggregators (last pointer is dangling)
		ASSERT_EQ(3u, context.singleStepAggregators().size());

		const auto& breadcrumbs1 = context.singleStepAggregators()[0]->breadcrumbs();
		ASSERT_EQ(2u, breadcrumbs1.size());
		EXPECT_EQ(MakeBreadcrumb(pMessage1->Signature.Root.ParentPublicKey, 1000), breadcrumbs1[0]);
		EXPECT_EQ(MakeBreadcrumb(pMessage4->Signature.Root.ParentPublicKey, 1100), breadcrumbs1[1]);

		const auto& breadcrumbs2 = context.singleStepAggregators()[1]->breadcrumbs();
		ASSERT_EQ(1u, breadcrumbs2.size());
		EXPECT_EQ(MakeBreadcrumb(pMessage2->Signature.Root.ParentPublicKey, 400), breadcrumbs2[0]);
	}

	TEST(TEST_CLASS, CanOnlyAddMultiStepMessagesWithStepAtLeastLastConsensusStep) {
		// Arrange:
		TestContext context(2000, 3000);
		auto& aggregator = context.multiStepAggregator();

		auto hashes = test::GenerateRandomDataVector<Hash256>(4);
		auto pMessage1 = CreateMessage({ 6, 4, 5 }, hashes[0]);
		auto pMessage2 = CreateMessage({ 8, 4, 5 }, hashes[1]); // greater step
		auto pMessage3 = CreateMessage({ 4, 4, 5 }, hashes[2]); // less step
		auto pMessage4 = CreateMessage({ 6, 4, 5 }, hashes[3]);

		// Act: reach consensus on { 6, 4, 5 }
		aggregator.add(*pMessage1, 2000);
		aggregator.add(*pMessage2, 400);
		aggregator.add(*pMessage3, 700);
		aggregator.add(*pMessage4, 100);

		// Assert: aggregator associated with { 4, 4, 5 } was never added
		EXPECT_EQ(2u, aggregator.size());
		EXPECT_EQ(MakeConsensusTuples({ 6, 4, 5, }, { hashes[0], hashes[3] }), context.consensusTuples());

		// - check single step aggregators
		ASSERT_EQ(2u, context.singleStepAggregators().size());

		const auto& breadcrumbs1 = context.singleStepAggregators()[0]->breadcrumbs();
		ASSERT_EQ(2u, breadcrumbs1.size());
		EXPECT_EQ(MakeBreadcrumb(pMessage1->Signature.Root.ParentPublicKey, 2000), breadcrumbs1[0]);
		EXPECT_EQ(MakeBreadcrumb(pMessage4->Signature.Root.ParentPublicKey, 100), breadcrumbs1[1]);

		const auto& breadcrumbs2 = context.singleStepAggregators()[1]->breadcrumbs();
		ASSERT_EQ(1u, breadcrumbs2.size());
		EXPECT_EQ(MakeBreadcrumb(pMessage2->Signature.Root.ParentPublicKey, 400), breadcrumbs2[0]);
	}

	TEST(TEST_CLASS, CanAddMultiStepMessagesThatReachConsensusAtMultipleSteps) {
		// Arrange:
		TestContext context(2000, 3000);
		auto& aggregator = context.multiStepAggregator();

		auto hashes = test::GenerateRandomDataVector<Hash256>(5);
		auto pMessage1 = CreateMessage({ 4, 4, 5 }, hashes[0]);
		auto pMessage2 = CreateMessage({ 4, 4, 4 }, hashes[1]);
		auto pMessage3 = CreateMessage({ 4, 4, 5 }, hashes[2]);
		auto pMessage4 = CreateMessage({ 8, 4, 5 }, hashes[3]);
		auto pMessage5 = CreateMessage({ 7, 7, 7 }, hashes[4]);

		// Act: message 1 consensus at { 4, 4, 5 }, message 4 consensus at { 8, 4, 5 }
		aggregator.add(*pMessage1, 2003);
		aggregator.add(*pMessage2, 2002);
		aggregator.add(*pMessage3, 100);
		aggregator.add(*pMessage4, 2001);
		aggregator.add(*pMessage5, 2004);

		// Assert:
		EXPECT_EQ(1u, aggregator.size());
		ConsensusTuples expectedConsensusTuples{
			{ { 4, 4, 5 }, hashes[0] },
			{ { 4, 4, 5 }, hashes[2] },
			{ { 8, 4, 5 }, hashes[3] }
		};
		EXPECT_EQ(expectedConsensusTuples, context.consensusTuples());

		// - check single step aggregators (first pointer is dangling)
		ASSERT_EQ(2u, context.singleStepAggregators().size());

		const auto& breadcrumbs2 = context.singleStepAggregators()[1]->breadcrumbs();
		ASSERT_EQ(1u, breadcrumbs2.size());
		EXPECT_EQ(MakeBreadcrumb(pMessage4->Signature.Root.ParentPublicKey, 2001), breadcrumbs2[0]);
	}

	// endregion
}}
