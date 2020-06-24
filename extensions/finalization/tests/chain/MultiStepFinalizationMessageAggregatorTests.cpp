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
#include "finalization/src/FinalizationConfiguration.h"
#include "finalization/tests/test/FinalizationMessageTestUtils.h"
#include "tests/TestHarness.h"

namespace catapult { namespace chain {

#define TEST_CLASS MultiStepFinalizationMessageAggregatorTests

	namespace {
		using FP = FinalizationPoint;

		// region ConsensusTuple(s)

		struct ConsensusTuple {
		public:
			crypto::StepIdentifier StepIdentifier;
			Hash256 Hash;
			std::vector<Key> SignerPublicKeys;

		public:
			bool operator==(const ConsensusTuple& rhs) const {
				return StepIdentifier == rhs.StepIdentifier && Hash == rhs.Hash && SignerPublicKeys == rhs.SignerPublicKeys;
			}

			friend std::ostream& operator<<(std::ostream& out, const ConsensusTuple& tuple) {
				out << "step " << tuple.StepIdentifier << " hash " << tuple.Hash << " { ";

				for (const auto& publicKey : tuple.SignerPublicKeys)
					out << publicKey << " ";

				out << "}";
				return out;
			}
		};

		using ConsensusTuples = std::vector<ConsensusTuple>;

		// endregion

		// region MockSingleStepFinalizationMessageAggregator

		Key GetSignerPublicKey(const model::FinalizationMessage& message) {
			return message.Signature.Root.ParentPublicKey;
		}

		class MockSingleStepFinalizationMessageAggregator : public SingleStepFinalizationMessageAggregator {
		public:
			MockSingleStepFinalizationMessageAggregator(
					const finalization::FinalizationConfiguration& config,
					const crypto::StepIdentifier& stepIdentifier)
					: m_config(config)
					, m_stepIdentifier(stepIdentifier)
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
			const auto& stepIdentifier() const {
				return m_stepIdentifier;
			}

			const auto& breadcrumbs() const {
				return m_breadcrumbs;
			}

		public:
			void add(const model::FinalizationMessage& message, uint64_t numVotes) override {
				m_breadcrumbs.emplace_back(GetSignerPublicKey(message), numVotes);

				m_numVotes += numVotes;
				if (m_numVotes >= m_config.Threshold) {
					m_consensusHash = *message.HashesPtr();
					m_hasConsensus = true;
				}
			}

		private:
			finalization::FinalizationConfiguration m_config;
			crypto::StepIdentifier m_stepIdentifier;
			bool m_hasConsensus;
			Hash256 m_consensusHash;

			uint64_t m_numVotes;
			std::vector<std::pair<Key, uint64_t>> m_breadcrumbs;
		};

		// endregion

		// region MessagesBuilder

		class MessagesBuilder {
		public:
			size_t size() const {
				return m_messages.size();
			}

			auto message(size_t index) const {
				return m_messages[index];
			}

			auto hash(size_t index) const {
				return m_hashes[index];
			}

			auto breadcrumbs(std::initializer_list<size_t> indexes) const {
				std::vector<std::pair<Key, uint64_t>> breadcrumbs;
				for (auto index : indexes)
					breadcrumbs.emplace_back(m_signerPublicKeys[index], m_processMessageResults[index].second);

				return breadcrumbs;
			}

			auto signerPublicKeys(std::initializer_list<size_t> indexes) const {
				std::vector<Key> signerPublicKeys;
				for (auto index : indexes)
					signerPublicKeys.push_back(m_signerPublicKeys[index]);

				return signerPublicKeys;
			}

			MessageProcessor createProcessor() const {
				return [&messages = m_messages, &processMessageResults = m_processMessageResults](const auto& message) {
					auto iter = std::find_if(messages.cbegin(), messages.cend(), [&message](const auto& pMessage) {
						return GetSignerPublicKey(message) == GetSignerPublicKey(*pMessage);
					});

					if (messages.cend() == iter)
						CATAPULT_THROW_INVALID_ARGUMENT("could not find message information");

					auto messageIndex = static_cast<size_t>(std::distance(messages.cbegin(), iter));
					return processMessageResults[messageIndex];
				};
			}

		public:
			void push(const crypto::StepIdentifier& stepIdentifier, uint64_t numVotes) {
				push(stepIdentifier, numVotes, model::ProcessMessageResult::Success);
			}

			void push(const crypto::StepIdentifier& stepIdentifier, uint64_t numVotes, model::ProcessMessageResult processMessageResult) {
				m_hashes.push_back(test::GenerateRandomByteArray<Hash256>());

				auto pMessage = test::CreateMessage(stepIdentifier, m_hashes.back());
				m_signerPublicKeys.push_back(GetSignerPublicKey(*pMessage));
				m_messages.push_back(std::move(pMessage));

				m_processMessageResults.emplace_back(processMessageResult, numVotes);
			}

		private:
			std::vector<Hash256> m_hashes;
			std::vector<Key> m_signerPublicKeys;
			std::vector<std::shared_ptr<model::FinalizationMessage>> m_messages;

			std::vector<std::pair<model::ProcessMessageResult, uint64_t>> m_processMessageResults;
		};

		// endregion

		// region TestContext

		class TestContext {
		public:
			TestContext(uint32_t threshold, uint32_t size, const MessageProcessor& messageProcessor) {
				auto config = finalization::FinalizationConfiguration::Uninitialized();
				config.Size = size;
				config.Threshold = threshold;

				m_pMultiStepAggregator = std::make_unique<MultiStepFinalizationMessageAggregator>(
						messageProcessor,
						createAggregatorFactory(config),
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
			SingleStepAggregatorFactory createAggregatorFactory(const finalization::FinalizationConfiguration& config) {
				return [config, &singleStepAggregators = m_singleStepAggregators](const auto& stepIdentifier) {
					auto pAggregator = std::make_unique<MockSingleStepFinalizationMessageAggregator>(config, stepIdentifier);
					singleStepAggregators.push_back(pAggregator.get());
					return pAggregator;
				};
			}

			ConsensusSink createConsensusSink() {
				return [&consensusTuples = m_consensusTuples](const auto& stepIdentifier, const auto& hash, const auto& proof) {
					ConsensusTuple consensusTuple;
					consensusTuple.StepIdentifier = stepIdentifier;
					consensusTuple.Hash = hash;

					for (const auto& pMessage : proof)
						consensusTuple.SignerPublicKeys.push_back(GetSignerPublicKey(*pMessage));

					consensusTuples.push_back(consensusTuple);
				};
			}

		private:
			std::unique_ptr<MultiStepFinalizationMessageAggregator> m_pMultiStepAggregator;
			std::vector<MockSingleStepFinalizationMessageAggregator*> m_singleStepAggregators;
			ConsensusTuples m_consensusTuples;
		};

		// endregion

		// region RunSinglePointMessagesTest

		struct SingleStepAggregatorDescriptor {
			bool IsValid;
			crypto::StepIdentifier StepIdentifier;
			std::initializer_list<size_t> BreadcrumbIndexes;
		};

		template<typename TTraits>
		ConsensusTuples RunSinglePointMessagesTest(
				const MessagesBuilder& messagesBuilder,
				FP point,
				size_t expectedAggregatorSize,
				const std::vector<SingleStepAggregatorDescriptor>& descriptors) {
			// Arrange:
			TestContext context(2000, 3000, messagesBuilder.createProcessor());
			auto& aggregator = context.multiStepAggregator();

			// Act:
			TTraits::AddAll(aggregator, point, messagesBuilder);

			// Assert:
			EXPECT_EQ(expectedAggregatorSize, aggregator.size());

			// - check single step aggregator
			EXPECT_EQ(descriptors.size(), context.singleStepAggregators().size());
			if (descriptors.size() != context.singleStepAggregators().size())
				CATAPULT_THROW_INVALID_ARGUMENT("unexpected number of single step aggregators");

			size_t i = 0;
			size_t numValidDescriptors = 0;
			for (const auto& descriptor : descriptors) {
				if (!descriptor.IsValid) {
					++i;
					continue;
				}

				const auto& singleStepAggregator = context.singleStepAggregators()[i];
				EXPECT_EQ(descriptor.StepIdentifier, singleStepAggregator->stepIdentifier()) << "at " << i;
				EXPECT_EQ(messagesBuilder.breadcrumbs(descriptor.BreadcrumbIndexes), singleStepAggregator->breadcrumbs()) << "at " << i;
				++numValidDescriptors;
				++i;
			}

			// Sanity:
			EXPECT_EQ(expectedAggregatorSize, numValidDescriptors);

			return context.consensusTuples();
		}

		// endregion
	}

	// region traits

	namespace {
		struct ProcessTraits {
			static void AddAll(MultiStepFinalizationMessageAggregator& aggregator, FP nextPoint, const MessagesBuilder& messagesBuilder) {
				aggregator.setNextFinalizationPoint(nextPoint);

				for (auto i = 0u; i < messagesBuilder.size(); ++i)
					aggregator.add(messagesBuilder.message(i));
			}
		};
	}

#define PROCESS_REPROCESS_TEST(TEST_NAME) \
	template<typename TTraits> void TRAITS_TEST_NAME(TEST_CLASS, TEST_NAME)(); \
	TEST(TEST_CLASS, TEST_NAME) { TRAITS_TEST_NAME(TEST_CLASS, TEST_NAME)<ProcessTraits>(); } \
	template<typename TTraits> void TRAITS_TEST_NAME(TEST_CLASS, TEST_NAME)()

	// endregion

	// region constructor

	TEST(TEST_CLASS, InitiallyAggregatorIsEmpty) {
		// Arrange:
		TestContext context(2000, 3000, MessageProcessor());

		// Act:
		const auto& aggregator = context.multiStepAggregator();

		// Assert:
		EXPECT_EQ(0u, aggregator.size());
		EXPECT_TRUE(context.consensusTuples().empty());
	}

	// endregion

	// region single step

	namespace {
		constexpr const auto Single_Step_Identifier = crypto::StepIdentifier{ 3, 4, 5 };

		template<typename TTraits>
		ConsensusTuples RunSingleStepMessagesTest(
				const MessagesBuilder& messagesBuilder,
				std::initializer_list<size_t> expectedBreadcrumbIndexes) {
			return RunSinglePointMessagesTest<TTraits>(messagesBuilder, FP(Single_Step_Identifier.Point), 1, {
				{ true, Single_Step_Identifier, expectedBreadcrumbIndexes }
			});
		}
	}

	PROCESS_REPROCESS_TEST(CanAddSingleStepMessagesThatDoNotReachConsensus) {
		// Arrange:
		MessagesBuilder messagesBuilder;
		for (auto numVotes : std::initializer_list<uint64_t>{ 1000, 400, 500 })
			messagesBuilder.push(Single_Step_Identifier, numVotes);

		// Act:
		auto consensusTuples = RunSingleStepMessagesTest<TTraits>(messagesBuilder, { 0, 1, 2 });

		// Assert:
		EXPECT_TRUE(consensusTuples.empty());
	}

	PROCESS_REPROCESS_TEST(CanAddSingleStepMessagesThatReachConsensus) {
		// Arrange:
		MessagesBuilder messagesBuilder;
		for (auto numVotes : std::initializer_list<uint64_t>{ 1000, 750, 250 })
			messagesBuilder.push(Single_Step_Identifier, numVotes);

		// Act:
		auto consensusTuples = RunSingleStepMessagesTest<TTraits>(messagesBuilder, { 0, 1, 2 });

		// Assert:
		ConsensusTuples expectedConsensusTuples{
			{ Single_Step_Identifier, messagesBuilder.hash(2), messagesBuilder.signerPublicKeys({ 0, 1, 2 }) }
		};
		EXPECT_EQ(expectedConsensusTuples, consensusTuples);
	}

	PROCESS_REPROCESS_TEST(CanAddSingleStepMessagesThatReachConsensusMultipleTimes) {
		// Arrange:
		MessagesBuilder messagesBuilder;
		for (auto numVotes : std::initializer_list<uint64_t>{ 2000, 1, 2 })
			messagesBuilder.push(Single_Step_Identifier, numVotes);

		// Act:
		auto consensusTuples = RunSingleStepMessagesTest<TTraits>(messagesBuilder, { 0, 1, 2 });

		// Assert:
		ConsensusTuples expectedConsensusTuples{
			{ Single_Step_Identifier, messagesBuilder.hash(0), messagesBuilder.signerPublicKeys({ 0 }) },
			{ Single_Step_Identifier, messagesBuilder.hash(1), messagesBuilder.signerPublicKeys({ 0, 1 }) },
			{ Single_Step_Identifier, messagesBuilder.hash(2), messagesBuilder.signerPublicKeys({ 0, 1, 2 }) }
		};
		EXPECT_EQ(expectedConsensusTuples, consensusTuples);
	}

	PROCESS_REPROCESS_TEST(CanOnlyAddSingleStepMessagesThatCanBeProcessedSuccessfully) {
		// Arrange:
		MessagesBuilder messagesBuilder;
		messagesBuilder.push(Single_Step_Identifier, 1750);
		messagesBuilder.push(Single_Step_Identifier, 500, model::ProcessMessageResult::Failure_Selection);
		messagesBuilder.push(Single_Step_Identifier, 300);
		messagesBuilder.push(Single_Step_Identifier, 100, model::ProcessMessageResult::Failure_Voter);

		// Act:
		auto consensusTuples = RunSingleStepMessagesTest<TTraits>(messagesBuilder, { 0, 2 });

		// Assert:
		ConsensusTuples expectedConsensusTuples{
			{ Single_Step_Identifier, messagesBuilder.hash(2), messagesBuilder.signerPublicKeys({ 0, 2 }) }
		};
		EXPECT_EQ(expectedConsensusTuples, consensusTuples);
	}

	// endregion

	// region multiple steps

	PROCESS_REPROCESS_TEST(CanAddMultiStepMessagesThatDoNotReachConsensus) {
		// Arrange:
		MessagesBuilder messagesBuilder;
		messagesBuilder.push({ 6, 4, 5 }, 1000);
		messagesBuilder.push({ 6, 8, 5 }, 400); // higher round
		messagesBuilder.push({ 6, 2, 5 }, 700); // lower round
		messagesBuilder.push({ 6, 4, 5 }, 900);

		// Act: aggregators are kept from all steps because no consensus is reached
		auto consensusTuples = RunSinglePointMessagesTest<TTraits>(messagesBuilder, FP(6), 3, {
			{ true, { 6, 4, 5 }, { 0, 3 } },
			{ true, { 6, 8, 5 }, { 1 } },
			{ true, { 6, 2, 5 }, { 2 } }
		});

		// Assert:
		EXPECT_TRUE(consensusTuples.empty());
	}

	PROCESS_REPROCESS_TEST(CanAddMultiStepMessagesThatReachConsensus) {
		// Arrange:
		MessagesBuilder messagesBuilder;
		messagesBuilder.push({ 6, 4, 5 }, 1000);
		messagesBuilder.push({ 6, 8, 5 }, 400); // higher round
		messagesBuilder.push({ 6, 2, 5 }, 700); // lower round
		messagesBuilder.push({ 6, 4, 5 }, 1100);

		// Act: only aggregators from steps no less than consensus step are kept
		auto consensusTuples = RunSinglePointMessagesTest<TTraits>(messagesBuilder, FP(6), 2, {
			{ true, { 6, 4, 5 }, { 0, 3 } },
			{ true, { 6, 8, 5 }, { 1 } },
			{ false, { 6, 2, 5 }, { 2 } }
		});

		// Assert:
		ConsensusTuples expectedConsensusTuples{
			{ { 6, 4, 5 }, messagesBuilder.hash(3), messagesBuilder.signerPublicKeys({ 0, 3 }) }
		};
		EXPECT_EQ(expectedConsensusTuples, consensusTuples);
	}

	PROCESS_REPROCESS_TEST(CanAddMultiStepMessagesThatReachConsensusMultipleTimes) {
		// Arrange:
		MessagesBuilder messagesBuilder;
		messagesBuilder.push({ 6, 4, 5 }, 2000);
		messagesBuilder.push({ 6, 8, 5 }, 400); // higher round
		messagesBuilder.push({ 6, 2, 5 }, 700); // lower round
		messagesBuilder.push({ 6, 4, 5 }, 100);

		// Act: { 6, 2, 5 } aggregator is not created because earlier step consensus was already reached
		auto consensusTuples = RunSinglePointMessagesTest<TTraits>(messagesBuilder, FP(6), 2, {
			{ true, { 6, 4, 5 }, { 0, 3 } },
			{ true, { 6, 8, 5 }, { 1 } }
		});

		// Assert:
		ConsensusTuples expectedConsensusTuples{
			{ { 6, 4, 5 }, messagesBuilder.hash(0), messagesBuilder.signerPublicKeys({ 0 }) },
			{ { 6, 4, 5 }, messagesBuilder.hash(3), messagesBuilder.signerPublicKeys({ 0, 3 }) }
		};
		EXPECT_EQ(expectedConsensusTuples, consensusTuples);
	}

	PROCESS_REPROCESS_TEST(CanAddMultiStepMessagesThatReachConsensusAtMultipleSteps) {
		// Arrange:
		MessagesBuilder messagesBuilder;
		messagesBuilder.push({ 6, 4, 5 }, 2000);
		messagesBuilder.push({ 6, 8, 5 }, 400); // higher round
		messagesBuilder.push({ 6, 2, 5 }, 700); // lower round
		messagesBuilder.push({ 6, 8, 8 }, 2100);

		// Act:
		auto consensusTuples = RunSinglePointMessagesTest<TTraits>(messagesBuilder, FP(6), 1, {
			{ false, { 6, 4, 5 }, { 0 } },
			{ false, { 6, 8, 5 }, { 1 } },
			{ true, { 6, 8, 8 }, { 3 } }
		});

		// Assert:
		ConsensusTuples expectedConsensusTuples{
			{ { 6, 4, 5 }, messagesBuilder.hash(0), messagesBuilder.signerPublicKeys({ 0 }) },
			{ { 6, 8, 8 }, messagesBuilder.hash(3), messagesBuilder.signerPublicKeys({ 3 }) }
		};
		EXPECT_EQ(expectedConsensusTuples, consensusTuples);
	}

	PROCESS_REPROCESS_TEST(CanOnlyAddMultiStepMessagesThatCanBeProcessedSuccessfully) {
		// Arrange:
		MessagesBuilder messagesBuilder;
		messagesBuilder.push({ 6, 4, 5 }, 1750);
		messagesBuilder.push({ 6, 8, 5 }, 500, model::ProcessMessageResult::Failure_Selection);
		messagesBuilder.push({ 6, 4, 5 }, 300);
		messagesBuilder.push({ 6, 4, 5 }, 100, model::ProcessMessageResult::Failure_Voter);

		// Act: { 6, 8, 5 } aggregator is not created because message processing failed
		auto consensusTuples = RunSinglePointMessagesTest<TTraits>(messagesBuilder, FP(6), 1, {
			{ true, { 6, 4, 5 }, { 0, 2 } }
		});

		// Assert:
		ConsensusTuples expectedConsensusTuples{
			{ { 6, 4, 5 }, messagesBuilder.hash(2), messagesBuilder.signerPublicKeys({ 0, 2 }) }
		};
		EXPECT_EQ(expectedConsensusTuples, consensusTuples);
	}

	PROCESS_REPROCESS_TEST(CannotAddMultiStepMessagesThatHaveUnexpectedFinalizationPoint) {
		// Arrange:
		MessagesBuilder messagesBuilder;
		messagesBuilder.push({ 6, 4, 5 }, 2000);
		messagesBuilder.push({ 8, 8, 5 }, 2500); // higher FP
		messagesBuilder.push({ 4, 2, 5 }, 2500); // lower FP
		messagesBuilder.push({ 6, 4, 5 }, 100);

		// Act: messages with different finalization points are ignored
		auto consensusTuples = RunSinglePointMessagesTest<TTraits>(messagesBuilder, FP(6), 1, {
			{ true, { 6, 4, 5 }, { 0, 3 } }
		});

		// Assert:
		ConsensusTuples expectedConsensusTuples{
			{ { 6, 4, 5 }, messagesBuilder.hash(0), messagesBuilder.signerPublicKeys({ 0 }) },
			{ { 6, 4, 5 }, messagesBuilder.hash(3), messagesBuilder.signerPublicKeys({ 0, 3 }) }
		};
		EXPECT_EQ(expectedConsensusTuples, consensusTuples);
	}

	// endregion

	// region message ownership

	PROCESS_REPROCESS_TEST(AggregatorExtendsMessageLifetimes) {
		// Arrange:
		MessagesBuilder messagesBuilder;
		messagesBuilder.push({ 6, 4, 5 }, 2000);
		messagesBuilder.push({ 6, 8, 5 }, 400); // higher round
		messagesBuilder.push({ 6, 2, 5 }, 700); // lower round
		messagesBuilder.push({ 6, 8, 8 }, 2100);

		TestContext context(2000, 3000, messagesBuilder.createProcessor());
		auto& aggregator = context.multiStepAggregator();

		// - calculate expected consensus tuples before destroying builder
		ConsensusTuples expectedConsensusTuples{
			{ { 6, 4, 5 }, messagesBuilder.hash(0), messagesBuilder.signerPublicKeys({ 0 }) },
			{ { 6, 8, 8 }, messagesBuilder.hash(3), messagesBuilder.signerPublicKeys({ 3 }) }
		};

		// Act:
		TTraits::AddAll(aggregator, FP(6), messagesBuilder);
		messagesBuilder = MessagesBuilder(); // destroy builder

		// Assert:
		EXPECT_EQ(1u, aggregator.size());
		EXPECT_EQ(expectedConsensusTuples, context.consensusTuples());
	}

	// endregion

	// region setNextFinalizationPoint

	TEST(TEST_CLASS, CannotSetNextFinalizationPointToSmallerValue) {
		// Arrange:
		MessagesBuilder messagesBuilder;
		messagesBuilder.push({ 6, 4, 5 }, 1100);
		messagesBuilder.push({ 8, 4, 5 }, 400);
		messagesBuilder.push({ 4, 4, 5 }, 700);

		TestContext context(2000, 3000, messagesBuilder.createProcessor());
		auto& aggregator = context.multiStepAggregator();

		ProcessTraits::AddAll(aggregator, FP(6), messagesBuilder);

		// Act + Assert:
		EXPECT_THROW(aggregator.setNextFinalizationPoint(FP(5)), catapult_invalid_argument);
	}

	TEST(TEST_CLASS, CannotSetNextFinalizationPointToSameValue) {
		// Arrange:
		MessagesBuilder messagesBuilder;
		messagesBuilder.push({ 6, 4, 5 }, 1100);
		messagesBuilder.push({ 8, 4, 5 }, 400);
		messagesBuilder.push({ 4, 4, 5 }, 700);

		TestContext context(2000, 3000, messagesBuilder.createProcessor());
		auto& aggregator = context.multiStepAggregator();

		ProcessTraits::AddAll(aggregator, FP(6), messagesBuilder);

		// Act:
		aggregator.setNextFinalizationPoint(FP(6));

		// Assert:
		EXPECT_EQ(1u, aggregator.size());
		EXPECT_TRUE(context.consensusTuples().empty());
	}

	TEST(TEST_CLASS, CanSetNextFinalizationPointToLargerValue) {
		// Arrange:
		MessagesBuilder messagesBuilder;
		messagesBuilder.push({ 6, 4, 5 }, 1100);
		messagesBuilder.push({ 8, 4, 5 }, 400);
		messagesBuilder.push({ 4, 4, 5 }, 700);

		TestContext context(2000, 3000, messagesBuilder.createProcessor());
		auto& aggregator = context.multiStepAggregator();

		ProcessTraits::AddAll(aggregator, FP(6), messagesBuilder);

		// Sanity:
		EXPECT_EQ(1u, aggregator.size());

		// Act:
		aggregator.setNextFinalizationPoint(FP(7));

		// Assert:
		EXPECT_EQ(0u, aggregator.size());
		EXPECT_TRUE(context.consensusTuples().empty());
	}

	// endregion
}}
