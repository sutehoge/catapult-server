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

#include "finalization/src/chain/FinalizationStageAdvancer.h"
#include "finalization/src/chain/MultiRoundMessageAggregator.h"
#include "finalization/tests/test/FinalizationMessageTestUtils.h"
#include "finalization/tests/test/mocks/MockRoundMessageAggregator.h"
#include "tests/TestHarness.h"

namespace catapult { namespace chain {

#define TEST_CLASS FinalizationStageAdvancerTests

	namespace {
		// region TestContext

		class TestContext {
		private:
			using RoundMessageAggregatorInitializer = consumer<mocks::MockRoundMessageAggregator&>;

		public:
			TestContext(FinalizationPoint point, Timestamp time, const utils::TimeSpan& stepDuration, uint64_t votingSetGrouping = 100)
					: TestContext(point, point, time, stepDuration, votingSetGrouping)
			{}

			TestContext(
					FinalizationPoint minPoint,
					FinalizationPoint maxPoint,
					Timestamp time,
					const utils::TimeSpan& stepDuration,
					uint64_t votingSetGrouping = 100) {
				m_pAggregator = std::make_unique<MultiRoundMessageAggregator>(
						10'000'000,
						minPoint,
						model::HeightHashPair(),
						[this](auto roundPoint, auto height) {
							auto pRoundMessageAggregator = std::make_unique<mocks::MockRoundMessageAggregator>(roundPoint, height);
							if (m_roundMessageAggregatorInitializer)
								m_roundMessageAggregatorInitializer(*pRoundMessageAggregator);

							return pRoundMessageAggregator;
						});

				// set the max point much higher than necessary in order to ensure that the advancer is not dependent on the
				// aggregator's current max point
				m_pAggregator->modifier().setMaxFinalizationPoint(maxPoint + FinalizationPoint(10));

				auto config = finalization::FinalizationConfiguration::Uninitialized();
				config.StepDuration = stepDuration;
				config.VotingSetGrouping = votingSetGrouping;

				m_pAdvancer = CreateFinalizationStageAdvancer(config, maxPoint, time, *m_pAggregator);
			}

		public:
			auto& aggregator() {
				return *m_pAggregator;
			}

			auto& advancer() {
				return *m_pAdvancer;
			}

		public:
			void setRoundMessageAggregatorInitializer(const RoundMessageAggregatorInitializer& roundMessageAggregatorInitializer) {
				m_roundMessageAggregatorInitializer = roundMessageAggregatorInitializer;
			}

		private:
			RoundMessageAggregatorInitializer m_roundMessageAggregatorInitializer;
			std::unique_ptr<MultiRoundMessageAggregator> m_pAggregator;

			std::unique_ptr<FinalizationStageAdvancer> m_pAdvancer;
		};

		// endregion
	}

	// region constructor

	TEST(TEST_CLASS, AllPredicatesReturnFalseAtStartTime) {
		// Arrange:
		TestContext context(FinalizationPoint(7), Timestamp(50), utils::TimeSpan::FromMilliseconds(100));

		// Act + Assert:
		EXPECT_FALSE(context.advancer().canSendPrevote(Timestamp(50)));

		model::HeightHashPair target;
		EXPECT_FALSE(context.advancer().canSendPrecommit(Timestamp(50), target));
		EXPECT_EQ(model::HeightHashPair(), target);

		EXPECT_FALSE(context.advancer().canStartNextRound());
	}

	// endregion

	// region canSendPrevote

	TEST(TEST_CLASS, CanSendPrevoteAtStepIntervalWhenRoundDoesNotExist) {
		// Arrange:
		TestContext context(FinalizationPoint(7), Timestamp(50), utils::TimeSpan::FromMilliseconds(100));

		// Act + Assert:
		for (auto value : std::initializer_list<uint64_t>{ 50, 100, 149 })
			EXPECT_FALSE(context.advancer().canSendPrevote(Timestamp(value))) << value;

		for (auto value : std::initializer_list<uint64_t>{ 150, 151, 250 })
			EXPECT_TRUE(context.advancer().canSendPrevote(Timestamp(value))) << value;
	}

	TEST(TEST_CLASS, CanSendPrevoteAtStepIntervalWhenRoundIsNotCompletable) {
		// Arrange:
		TestContext context(FinalizationPoint(7), Timestamp(50), utils::TimeSpan::FromMilliseconds(100));
		context.setRoundMessageAggregatorInitializer([](auto& roundMessageAggregator) {
			auto hash = test::GenerateRandomByteArray<Hash256>();
			roundMessageAggregator.roundContext().acceptPrevote(Height(246), &hash, 1, 500);
		});

		context.aggregator().modifier().add(test::CreateMessage(FinalizationPoint(7)));

		// Act + Assert:
		for (auto value : std::initializer_list<uint64_t>{ 50, 100, 149 })
			EXPECT_FALSE(context.advancer().canSendPrevote(Timestamp(value))) << value;

		for (auto value : std::initializer_list<uint64_t>{ 150, 151, 250 })
			EXPECT_TRUE(context.advancer().canSendPrevote(Timestamp(value))) << value;
	}

	TEST(TEST_CLASS, CanSendPrevoteWhenRoundIsCompletable) {
		// Arrange:
		TestContext context(FinalizationPoint(7), Timestamp(50), utils::TimeSpan::FromMilliseconds(100));
		context.setRoundMessageAggregatorInitializer([](auto& roundMessageAggregator) {
			auto hash = test::GenerateRandomByteArray<Hash256>();
			roundMessageAggregator.roundContext().acceptPrevote(Height(246), &hash, 1, 750);
			roundMessageAggregator.roundContext().acceptPrecommit(Height(246), hash, 750);
		});

		context.aggregator().modifier().add(test::CreateMessage(FinalizationPoint(7)));

		// Act + Assert:
		for (auto value : std::initializer_list<uint64_t>{ 50, 100, 149, 150, 151, 250 })
			EXPECT_TRUE(context.advancer().canSendPrevote(Timestamp(value))) << value;
	}

	// endregion

	// region canSendPrecommit

	TEST(TEST_CLASS, CannotSendPrecommitWhenRoundDoesNotExist) {
		// Arrange:
		TestContext context(FinalizationPoint(7), Timestamp(50), utils::TimeSpan::FromMilliseconds(100));

		// Act + Assert:
		for (auto value : std::initializer_list<uint64_t>{ 50, 150, 249, 250, 251, 350 }) {
			model::HeightHashPair target;
			EXPECT_FALSE(context.advancer().canSendPrecommit(Timestamp(value), target)) << value;
			EXPECT_EQ(model::HeightHashPair(), target);
		}
	}

	TEST(TEST_CLASS, CannotSendPrecommitWhenBestPrevoteDoesNotExist) {
		// Arrange:
		TestContext context(FinalizationPoint(7), Timestamp(50), utils::TimeSpan::FromMilliseconds(100));
		context.setRoundMessageAggregatorInitializer([](auto& roundMessageAggregator) {
			auto hash = test::GenerateRandomByteArray<Hash256>();
			roundMessageAggregator.roundContext().acceptPrevote(Height(246), &hash, 1, 500);
		});

		context.aggregator().modifier().add(test::CreateMessage(FinalizationPoint(7)));

		// Act + Assert:
		for (auto value : std::initializer_list<uint64_t>{ 50, 150, 249, 250, 251, 350 }) {
			model::HeightHashPair target;
			EXPECT_FALSE(context.advancer().canSendPrecommit(Timestamp(value), target)) << value;
			EXPECT_EQ(model::HeightHashPair(), target);
		}
	}

	TEST(TEST_CLASS, CannotSendPrecommitWhenBestPrevoteIsNotDescendantOfPreviousRoundEstimate) {
		// Arrange:
		auto hash = test::GenerateRandomByteArray<Hash256>();
		TestContext context(FinalizationPoint(6), FinalizationPoint(7), Timestamp(50), utils::TimeSpan::FromMilliseconds(100));
		context.setRoundMessageAggregatorInitializer([&hash](auto& roundMessageAggregator) {
			if (FinalizationPoint(6) == roundMessageAggregator.point()) {
				auto hashes = std::vector<Hash256>{ hash, test::GenerateRandomByteArray<Hash256>() };
				roundMessageAggregator.roundContext().acceptPrevote(Height(245), hashes.data(), hashes.size(), 750);
			} else {
				auto hashes = std::vector<Hash256>{ hash, test::GenerateRandomByteArray<Hash256>() };
				roundMessageAggregator.roundContext().acceptPrevote(Height(245), hashes.data(), hashes.size(), 750);
				roundMessageAggregator.roundContext().acceptPrecommit(Height(246), hashes[1], 750);
			}
		});

		context.aggregator().modifier().add(test::CreateMessage(FinalizationPoint(6)));
		context.aggregator().modifier().add(test::CreateMessage(FinalizationPoint(7)));

		// Act + Assert:
		for (auto value : std::initializer_list<uint64_t>{ 50, 150, 249, 250, 251, 350 }) {
			model::HeightHashPair target;
			EXPECT_FALSE(context.advancer().canSendPrecommit(Timestamp(value), target)) << value;
			EXPECT_EQ(model::HeightHashPair(), target);
		}
	}

	TEST(TEST_CLASS, CanSendPrecommitAtDoubleStepIntervalWhenBestPrevoteIsDescendant) {
		// Arrange:
		auto hash1 = test::GenerateRandomByteArray<Hash256>();
		auto hash2 = test::GenerateRandomByteArray<Hash256>();
		TestContext context(FinalizationPoint(6), FinalizationPoint(7), Timestamp(50), utils::TimeSpan::FromMilliseconds(100));
		context.setRoundMessageAggregatorInitializer([&hash1, &hash2](auto& roundMessageAggregator) {
			if (FinalizationPoint(6) == roundMessageAggregator.point()) {
				auto hashes = std::vector<Hash256>{ hash1, test::GenerateRandomByteArray<Hash256>() };
				roundMessageAggregator.roundContext().acceptPrevote(Height(245), &hashes[0], 1, 750);
				roundMessageAggregator.roundContext().acceptPrevote(Height(246), &hashes[1], 1, 150);
			} else {
				auto hashes = std::vector<Hash256>{ hash1, hash2 };
				roundMessageAggregator.roundContext().acceptPrevote(Height(245), hashes.data(), hashes.size(), 750);
			}
		});

		context.aggregator().modifier().add(test::CreateMessage(FinalizationPoint(6)));
		context.aggregator().modifier().add(test::CreateMessage(FinalizationPoint(7)));

		// Act + Assert:
		for (auto value : std::initializer_list<uint64_t>{ 50, 150, 249 }) {
			model::HeightHashPair target;
			EXPECT_FALSE(context.advancer().canSendPrecommit(Timestamp(value), target)) << value;
			EXPECT_EQ(model::HeightHashPair(), target);
		}

		for (auto value : std::initializer_list<uint64_t>{ 250, 251, 350 }) {
			model::HeightHashPair target;
			EXPECT_TRUE(context.advancer().canSendPrecommit(Timestamp(value), target)) << value;
			EXPECT_EQ(model::HeightHashPair({ Height(246), hash2 }), target);
		}
	}

	TEST(TEST_CLASS, CanSendPrecommitWhenBestPrevoteIsDescendantAndRoundIsCompletable) {
		// Arrange:
		auto hash1 = test::GenerateRandomByteArray<Hash256>();
		auto hash2 = test::GenerateRandomByteArray<Hash256>();
		TestContext context(FinalizationPoint(6), FinalizationPoint(7), Timestamp(50), utils::TimeSpan::FromMilliseconds(100));
		context.setRoundMessageAggregatorInitializer([&hash1, &hash2](auto& roundMessageAggregator) {
			if (FinalizationPoint(6) == roundMessageAggregator.point()) {
				auto hashes = std::vector<Hash256>{ hash1, test::GenerateRandomByteArray<Hash256>() };
				roundMessageAggregator.roundContext().acceptPrevote(Height(245), &hashes[0], 1, 750);
				roundMessageAggregator.roundContext().acceptPrevote(Height(246), &hashes[1], 1, 150);
			} else {
				auto hashes = std::vector<Hash256>{ hash1, hash2 };
				roundMessageAggregator.roundContext().acceptPrevote(Height(245), hashes.data(), hashes.size(), 750);
				roundMessageAggregator.roundContext().acceptPrecommit(Height(246), hashes[1], 750);
			}
		});

		context.aggregator().modifier().add(test::CreateMessage(FinalizationPoint(6)));
		context.aggregator().modifier().add(test::CreateMessage(FinalizationPoint(7)));

		// Act + Assert:
		for (auto value : std::initializer_list<uint64_t>{ 50, 150, 249, 250, 251, 350 }) {
			model::HeightHashPair target;
			EXPECT_TRUE(context.advancer().canSendPrecommit(Timestamp(value), target)) << value;
			EXPECT_EQ(model::HeightHashPair({ Height(246), hash2 }), target);
		}
	}

	// endregion

	// region canStartNextRound

	TEST(TEST_CLASS, CannotStartNextRoundWhenRoundDoesNotExist) {
		// Arrange:
		TestContext context(FinalizationPoint(7), Timestamp(50), utils::TimeSpan::FromMilliseconds(100));

		// Act + Assert:
		EXPECT_FALSE(context.advancer().canStartNextRound());
	}

	TEST(TEST_CLASS, CannotStartNextRoundWhenRoundIsNotCompletable) {
		// Arrange:
		TestContext context(FinalizationPoint(7), Timestamp(50), utils::TimeSpan::FromMilliseconds(100));
		context.setRoundMessageAggregatorInitializer([](auto& roundMessageAggregator) {
			auto hash = test::GenerateRandomByteArray<Hash256>();
			roundMessageAggregator.roundContext().acceptPrevote(Height(246), &hash, 1, 500);
		});

		context.aggregator().modifier().add(test::CreateMessage(FinalizationPoint(7)));

		// Act + Assert:
		EXPECT_FALSE(context.advancer().canStartNextRound());
	}

	TEST(TEST_CLASS, CanStartNextRoundWhenRoundIsCompletableAndEstimateDoesNotEndVotingSet) {
		// Arrange: estimate is 246, VotingSetGrouping is 100
		TestContext context(FinalizationPoint(7), Timestamp(50), utils::TimeSpan::FromMilliseconds(100));
		context.setRoundMessageAggregatorInitializer([](auto& roundMessageAggregator) {
			auto hash = test::GenerateRandomByteArray<Hash256>();
			roundMessageAggregator.roundContext().acceptPrevote(Height(246), &hash, 1, 750);
			roundMessageAggregator.roundContext().acceptPrecommit(Height(246), hash, 750);
		});

		context.aggregator().modifier().add(test::CreateMessage(FinalizationPoint(7)));

		// Act + Assert:
		EXPECT_TRUE(context.advancer().canStartNextRound());
	}

	TEST(TEST_CLASS, CannotStartNextRoundWhenRoundIsCompletableAndEstimateEndsVotingSetButThereIsNoBestPrecommit) {
		// Arrange: estimate is 246, VotingSetGrouping is 246
		TestContext context(FinalizationPoint(7), Timestamp(50), utils::TimeSpan::FromMilliseconds(100), 246);
		context.setRoundMessageAggregatorInitializer([](auto& roundMessageAggregator) {
			auto hash = test::GenerateRandomByteArray<Hash256>();
			roundMessageAggregator.roundContext().acceptPrevote(Height(246), &hash, 1, 750);
			roundMessageAggregator.roundContext().acceptPrecommit(Height(246), hash, 500);
		});

		context.aggregator().modifier().add(test::CreateMessage(FinalizationPoint(7)));

		// Act + Assert:
		EXPECT_FALSE(context.advancer().canStartNextRound());
	}

	TEST(TEST_CLASS, CanStartNextRoundWhenRoundIsCompletableAndBothEstimateAndBestPrecommitEndVotingSet) {
		// Arrange: estimate is 246, VotingSetGrouping is 246
		TestContext context(FinalizationPoint(7), Timestamp(50), utils::TimeSpan::FromMilliseconds(100), 246);
		context.setRoundMessageAggregatorInitializer([](auto& roundMessageAggregator) {
			auto hash = test::GenerateRandomByteArray<Hash256>();
			roundMessageAggregator.roundContext().acceptPrevote(Height(246), &hash, 1, 750);
			roundMessageAggregator.roundContext().acceptPrecommit(Height(246), hash, 750);
		});

		context.aggregator().modifier().add(test::CreateMessage(FinalizationPoint(7)));

		// Act + Assert:
		EXPECT_TRUE(context.advancer().canStartNextRound());
	}

	TEST(TEST_CLASS, CannotStartNextRoundWhenRoundIsCompletableAndEstimateButNotBestPrecommitEndVotingSet) {
		// Arrange: estimate is 246, VotingSetGrouping is 246
		TestContext context(FinalizationPoint(7), Timestamp(50), utils::TimeSpan::FromMilliseconds(100), 246);
		context.setRoundMessageAggregatorInitializer([](auto& roundMessageAggregator) {
			auto hashes = test::GenerateRandomDataVector<Hash256>(2);
			roundMessageAggregator.roundContext().acceptPrevote(Height(245), hashes.data(), 2, 750);
			roundMessageAggregator.roundContext().acceptPrecommit(Height(245), hashes[0], 200);
			roundMessageAggregator.roundContext().acceptPrecommit(Height(246), hashes[1], 500);
		});

		context.aggregator().modifier().add(test::CreateMessage(FinalizationPoint(7)));

		// Act + Assert:
		EXPECT_FALSE(context.advancer().canStartNextRound());
	}

	// endregion
}}
