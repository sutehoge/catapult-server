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
#include "catapult/model/HeightHashPair.h"
#include "finalization/tests/test/FinalizationMessageTestUtils.h"
#include "tests/test/nodeps/LockTestUtils.h"
#include "tests/TestHarness.h"

namespace catapult { namespace chain {

#define TEST_CLASS MultiStepFinalizationMessageAggregatorTests

	namespace {
		using FP = FinalizationPoint;

		constexpr auto Default_Height = Height(123);

		// region ConsensusTuple(s)

		struct ConsensusTuple {
		public:
			crypto::StepIdentifier StepIdentifier;
			catapult::Height Height;
			Hash256 Hash;
			std::vector<Key> SignerPublicKeys;

		public:
			bool operator==(const ConsensusTuple& rhs) const {
				return StepIdentifier == rhs.StepIdentifier && Hash == rhs.Hash && SignerPublicKeys == rhs.SignerPublicKeys;
			}

			friend std::ostream& operator<<(std::ostream& out, const ConsensusTuple& tuple) {
				out << "step " << tuple.StepIdentifier << " height " << tuple.Height << " hash " << tuple.Hash << " { ";

				for (const auto& publicKey : tuple.SignerPublicKeys)
					out << publicKey << " ";

				out << "}";
				return out;
			}
		};

		using ConsensusTuples = std::vector<ConsensusTuple>;

		// endregion

		// region MockSingleStepFinalizationMessageAggregator

		enum class ReductionMode { None, Choose_Last };

		Key GetSignerPublicKey(const model::FinalizationMessage& message) {
			return message.Signature.Root.ParentPublicKey;
		}

		class MockSingleStepFinalizationMessageAggregator : public SingleStepFinalizationMessageAggregator {
		public:
			MockSingleStepFinalizationMessageAggregator(
					ReductionMode reductionMode,
					const finalization::FinalizationConfiguration& config,
					const crypto::StepIdentifier& stepIdentifier)
					: m_reductionMode(reductionMode)
					, m_config(config)
					, m_stepIdentifier(stepIdentifier)
					, m_hasConsensus(false)
					, m_numVotes(0)
			{}

		public:
			bool hasConsensus() const override {
				return m_hasConsensus;
			}

			Height consensusHeight() const override {
				return m_consensusHeight;
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
			void reduce(FinalizationProof& proof) override {
				if (ReductionMode::None == m_reductionMode)
					return;

				auto pLastMessage = proof.back();
				proof = { pLastMessage };
			}

			void add(const model::FinalizationMessage& message, uint64_t numVotes) override {
				m_breadcrumbs.emplace_back(GetSignerPublicKey(message), numVotes);

				m_numVotes += numVotes;
				if (m_numVotes >= m_config.Threshold) {
					m_consensusHeight = message.Height;
					m_consensusHash = *message.HashesPtr();
					m_hasConsensus = true;
				}
			}

		private:
			ReductionMode m_reductionMode;
			finalization::FinalizationConfiguration m_config;
			crypto::StepIdentifier m_stepIdentifier;
			bool m_hasConsensus;
			uint64_t m_numVotes;

			Height m_consensusHeight;
			Hash256 m_consensusHash;
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
				pMessage->Height = Default_Height;
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

		struct TestContextOptions {
			uint64_t MaxResponseSize = 10'000'000;
			chain::ReductionMode ReductionMode = chain::ReductionMode::None;
		};

		class TestContext {
		public:
			TestContext(uint32_t threshold, uint32_t size, const MessageProcessor& messageProcessor)
					: TestContext(threshold, size, TestContextOptions(), messageProcessor)
			{}

			TestContext(uint32_t threshold, uint32_t size, const TestContextOptions& options, const MessageProcessor& messageProcessor) {
				auto config = finalization::FinalizationConfiguration::Uninitialized();
				config.Size = size;
				config.Threshold = threshold;

				m_pMultiStepAggregator = std::make_unique<MultiStepFinalizationMessageAggregator>(
						options.MaxResponseSize,
						messageProcessor,
						createAggregatorFactory(options.ReductionMode, config),
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
			SingleStepAggregatorFactory createAggregatorFactory(
					ReductionMode reductionMode,
					const finalization::FinalizationConfiguration& config) {
				return [reductionMode, config, &singleStepAggregators = m_singleStepAggregators](const auto& stepIdentifier) {
					auto pAggregator = std::make_unique<MockSingleStepFinalizationMessageAggregator>(
							reductionMode,
							config,
							stepIdentifier);
					singleStepAggregators.push_back(pAggregator.get());
					return pAggregator;
				};
			}

			ConsensusSink createConsensusSink() {
				return [&consensusTuples = m_consensusTuples](const auto& stepIdentifier, const auto& heightHashPair, const auto& proof) {
					ConsensusTuple consensusTuple;
					consensusTuple.StepIdentifier = stepIdentifier;
					consensusTuple.Height = heightHashPair.Height;
					consensusTuple.Hash = heightHashPair.Hash;

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
				const crypto::StepIdentifier& expectedMinStepIdentifier,
				const std::vector<SingleStepAggregatorDescriptor>& descriptors) {
			// Arrange:
			TestContextOptions options;
			options.ReductionMode = TTraits::Reduction_Mode;
			TestContext context(2000, 3000, options, messagesBuilder.createProcessor());
			auto& aggregator = context.multiStepAggregator();

			// Act:
			TTraits::AddAll(aggregator, point, messagesBuilder);

			// Assert:
			EXPECT_EQ(expectedAggregatorSize, aggregator.view().size());
			EXPECT_EQ(expectedMinStepIdentifier, aggregator.view().minStepIdentifier());

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
				aggregator.modifier().setNextFinalizationPoint(nextPoint);

				for (auto i = 0u; i < messagesBuilder.size(); ++i)
					aggregator.modifier().add(messagesBuilder.message(i));
			}
		};

		template<typename TTraits, ReductionMode Reduction_Mode_Value>
		struct ReductionModeTraitsDecorator : public TTraits {
			static constexpr auto Reduction_Mode = Reduction_Mode_Value;
		};

		template<typename TTraits>
		using ReductionNoneTraits = ReductionModeTraitsDecorator<TTraits, ReductionMode::None>;

		template<typename TTraits>
		using ReductionChooseLastTraits = ReductionModeTraitsDecorator<TTraits, ReductionMode::Choose_Last>;
	}

#define PROCESS_REPROCESS_TEST(TEST_NAME) \
	template<typename TTraits> void TRAITS_TEST_NAME(TEST_CLASS, TEST_NAME)(); \
	TEST(TEST_CLASS, TEST_NAME) { TRAITS_TEST_NAME(TEST_CLASS, TEST_NAME)<ReductionNoneTraits<ProcessTraits>>(); } \
	template<typename TTraits> void TRAITS_TEST_NAME(TEST_CLASS, TEST_NAME)()

	// endregion

	// region constructor

	TEST(TEST_CLASS, InitiallyAggregatorIsEmpty) {
		// Arrange:
		TestContext context(2000, 3000, MessageProcessor());

		// Act:
		const auto& aggregator = context.multiStepAggregator();

		// Assert:
		EXPECT_EQ(0u, aggregator.view().size());
		EXPECT_EQ(crypto::StepIdentifier({ 0, 0, 0 }), aggregator.view().minStepIdentifier());

		EXPECT_TRUE(context.consensusTuples().empty());
	}

	// endregion

	// region single step

	namespace {
		constexpr const auto Single_Step_Identifier = crypto::StepIdentifier{ 3, 4, 5 };

		template<typename TTraits>
		ConsensusTuples RunSingleStepMessagesTest(
				const MessagesBuilder& messagesBuilder,
				const crypto::StepIdentifier& expectedMinStepIdentifier,
				std::initializer_list<size_t> expectedBreadcrumbIndexes) {
			return RunSinglePointMessagesTest<TTraits>(messagesBuilder, FP(Single_Step_Identifier.Point), 1, expectedMinStepIdentifier, {
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
		auto consensusTuples = RunSingleStepMessagesTest<TTraits>(messagesBuilder, { Single_Step_Identifier.Point, 0, 0 }, { 0, 1, 2 });

		// Assert:
		EXPECT_TRUE(consensusTuples.empty());
	}

	PROCESS_REPROCESS_TEST(CanAddSingleStepMessagesThatReachConsensus) {
		// Arrange:
		MessagesBuilder messagesBuilder;
		for (auto numVotes : std::initializer_list<uint64_t>{ 1000, 750, 250 })
			messagesBuilder.push(Single_Step_Identifier, numVotes);

		// Act:
		auto consensusTuples = RunSingleStepMessagesTest<TTraits>(messagesBuilder, Single_Step_Identifier, { 0, 1, 2 });

		// Assert:
		ConsensusTuples expectedConsensusTuples{
			{ Single_Step_Identifier, Default_Height, messagesBuilder.hash(2), messagesBuilder.signerPublicKeys({ 0, 1, 2 }) }
		};
		EXPECT_EQ(expectedConsensusTuples, consensusTuples);
	}

	PROCESS_REPROCESS_TEST(CanAddSingleStepMessagesThatReachConsensusMultipleTimes) {
		// Arrange:
		MessagesBuilder messagesBuilder;
		for (auto numVotes : std::initializer_list<uint64_t>{ 2000, 1, 2 })
			messagesBuilder.push(Single_Step_Identifier, numVotes);

		// Act:
		auto consensusTuples = RunSingleStepMessagesTest<TTraits>(messagesBuilder, Single_Step_Identifier, { 0, 1, 2 });

		// Assert:
		ConsensusTuples expectedConsensusTuples{
			{ Single_Step_Identifier, Default_Height, messagesBuilder.hash(0), messagesBuilder.signerPublicKeys({ 0 }) },
			{ Single_Step_Identifier, Default_Height, messagesBuilder.hash(1), messagesBuilder.signerPublicKeys({ 0, 1 }) },
			{ Single_Step_Identifier, Default_Height, messagesBuilder.hash(2), messagesBuilder.signerPublicKeys({ 0, 1, 2 }) }
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
		auto consensusTuples = RunSingleStepMessagesTest<TTraits>(messagesBuilder, Single_Step_Identifier, { 0, 2 });

		// Assert:
		ConsensusTuples expectedConsensusTuples{
			{ Single_Step_Identifier, Default_Height, messagesBuilder.hash(2), messagesBuilder.signerPublicKeys({ 0, 2 }) }
		};
		EXPECT_EQ(expectedConsensusTuples, consensusTuples);
	}

	// endregion

	// region single step - with reduction

	PROCESS_REPROCESS_TEST(CanAddSingleStepMessagesThatReachConsensus_WithReduction) {
		// Arrange:
		MessagesBuilder messagesBuilder;
		for (auto numVotes : std::initializer_list<uint64_t>{ 1000, 750, 250 })
			messagesBuilder.push(Single_Step_Identifier, numVotes);

		// Act:
		auto consensusTuples = RunSingleStepMessagesTest<ReductionChooseLastTraits<TTraits>>(
				messagesBuilder,
				Single_Step_Identifier,
				{ 0, 1, 2 });

		// Assert:
		ConsensusTuples expectedConsensusTuples{
			{ Single_Step_Identifier, Default_Height, messagesBuilder.hash(2), messagesBuilder.signerPublicKeys({ 2 }) }
		};
		EXPECT_EQ(expectedConsensusTuples, consensusTuples);
	}

	PROCESS_REPROCESS_TEST(CanAddSingleStepMessagesThatReachConsensusMultipleTimes_WithReduction) {
		// Arrange:
		MessagesBuilder messagesBuilder;
		for (auto numVotes : std::initializer_list<uint64_t>{ 2000, 1, 2 })
			messagesBuilder.push(Single_Step_Identifier, numVotes);

		// Act:
		auto consensusTuples = RunSingleStepMessagesTest<ReductionChooseLastTraits<TTraits>>(
				messagesBuilder,
				Single_Step_Identifier,
				{ 0, 1, 2 });

		// Assert:
		ConsensusTuples expectedConsensusTuples{
			{ Single_Step_Identifier, Default_Height, messagesBuilder.hash(0), messagesBuilder.signerPublicKeys({ 0 }) },
			{ Single_Step_Identifier, Default_Height, messagesBuilder.hash(1), messagesBuilder.signerPublicKeys({ 1 }) },
			{ Single_Step_Identifier, Default_Height, messagesBuilder.hash(2), messagesBuilder.signerPublicKeys({ 2 }) }
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
		auto consensusTuples = RunSinglePointMessagesTest<TTraits>(messagesBuilder, FP(6), 3, { 6, 0, 0 }, {
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
		auto consensusTuples = RunSinglePointMessagesTest<TTraits>(messagesBuilder, FP(6), 2, { 6, 4, 5 }, {
			{ true, { 6, 4, 5 }, { 0, 3 } },
			{ true, { 6, 8, 5 }, { 1 } },
			{ false, { 6, 2, 5 }, { 2 } }
		});

		// Assert:
		ConsensusTuples expectedConsensusTuples{
			{ { 6, 4, 5 }, Default_Height, messagesBuilder.hash(3), messagesBuilder.signerPublicKeys({ 0, 3 }) }
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
		auto consensusTuples = RunSinglePointMessagesTest<TTraits>(messagesBuilder, FP(6), 2, { 6, 4, 5 }, {
			{ true, { 6, 4, 5 }, { 0, 3 } },
			{ true, { 6, 8, 5 }, { 1 } }
		});

		// Assert:
		ConsensusTuples expectedConsensusTuples{
			{ { 6, 4, 5 }, Default_Height, messagesBuilder.hash(0), messagesBuilder.signerPublicKeys({ 0 }) },
			{ { 6, 4, 5 }, Default_Height, messagesBuilder.hash(3), messagesBuilder.signerPublicKeys({ 0, 3 }) }
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
		auto consensusTuples = RunSinglePointMessagesTest<TTraits>(messagesBuilder, FP(6), 1, { 6, 8, 8 }, {
			{ false, { 6, 4, 5 }, { 0 } },
			{ false, { 6, 8, 5 }, { 1 } },
			{ true, { 6, 8, 8 }, { 3 } }
		});

		// Assert:
		ConsensusTuples expectedConsensusTuples{
			{ { 6, 4, 5 }, Default_Height, messagesBuilder.hash(0), messagesBuilder.signerPublicKeys({ 0 }) },
			{ { 6, 8, 8 }, Default_Height, messagesBuilder.hash(3), messagesBuilder.signerPublicKeys({ 3 }) }
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
		auto consensusTuples = RunSinglePointMessagesTest<TTraits>(messagesBuilder, FP(6), 1, { 6, 4, 5 }, {
			{ true, { 6, 4, 5 }, { 0, 2 } }
		});

		// Assert:
		ConsensusTuples expectedConsensusTuples{
			{ { 6, 4, 5 }, Default_Height, messagesBuilder.hash(2), messagesBuilder.signerPublicKeys({ 0, 2 }) }
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
		auto consensusTuples = RunSinglePointMessagesTest<TTraits>(messagesBuilder, FP(6), 1, { 6, 4, 5 }, {
			{ true, { 6, 4, 5 }, { 0, 3 } }
		});

		// Assert:
		ConsensusTuples expectedConsensusTuples{
			{ { 6, 4, 5 }, Default_Height, messagesBuilder.hash(0), messagesBuilder.signerPublicKeys({ 0 }) },
			{ { 6, 4, 5 }, Default_Height, messagesBuilder.hash(3), messagesBuilder.signerPublicKeys({ 0, 3 }) }
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
			{ { 6, 4, 5 }, Default_Height, messagesBuilder.hash(0), messagesBuilder.signerPublicKeys({ 0 }) },
			{ { 6, 8, 8 }, Default_Height, messagesBuilder.hash(3), messagesBuilder.signerPublicKeys({ 3 }) }
		};

		// Act:
		TTraits::AddAll(aggregator, FP(6), messagesBuilder);
		messagesBuilder = MessagesBuilder(); // destroy builder

		// Assert:
		EXPECT_EQ(1u, aggregator.view().size());
		EXPECT_EQ(crypto::StepIdentifier({ 6, 8, 8 }), aggregator.view().minStepIdentifier());

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

		// Sanity:
		EXPECT_EQ(1u, aggregator.view().size());
		EXPECT_EQ(crypto::StepIdentifier({ 6, 0, 0 }), aggregator.view().minStepIdentifier());

		// Act + Assert:
		EXPECT_THROW(aggregator.modifier().setNextFinalizationPoint(FP(5)), catapult_invalid_argument);

		EXPECT_EQ(1u, aggregator.view().size());
		EXPECT_EQ(crypto::StepIdentifier({ 6, 0, 0 }), aggregator.view().minStepIdentifier());
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

		// Sanity:
		EXPECT_EQ(1u, aggregator.view().size());
		EXPECT_EQ(crypto::StepIdentifier({ 6, 0, 0 }), aggregator.view().minStepIdentifier());

		// Act:
		aggregator.modifier().setNextFinalizationPoint(FP(6));

		// Assert:
		EXPECT_EQ(1u, aggregator.view().size());
		EXPECT_EQ(crypto::StepIdentifier({ 6, 0, 0 }), aggregator.view().minStepIdentifier());

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
		EXPECT_EQ(1u, aggregator.view().size());
		EXPECT_EQ(crypto::StepIdentifier({ 6, 0, 0 }), aggregator.view().minStepIdentifier());

		// Act:
		aggregator.modifier().setNextFinalizationPoint(FP(7));

		// Assert:
		EXPECT_EQ(0u, aggregator.view().size());
		EXPECT_EQ(crypto::StepIdentifier({ 7, 0, 0 }), aggregator.view().minStepIdentifier());

		EXPECT_TRUE(context.consensusTuples().empty());
	}

	// endregion

	// region shortHashes

	namespace {
		auto ToShortHashes(const MessagesBuilder& messagesBuilder) {
			std::vector<utils::ShortHash> shortHashes;
			for (auto i = 0u; i < messagesBuilder.size(); ++i)
				shortHashes.push_back(utils::ToShortHash(model::CalculateMessageHash(*messagesBuilder.message(i))));

			return shortHashes;
		}
	}

	TEST(TEST_CLASS, ShortHashesReturnsNoShortHashesWhenAggregtorIsEmpty) {
		// Arrange:
		MessagesBuilder messagesBuilder;

		TestContext context(2000, 3000, messagesBuilder.createProcessor());
		auto& aggregator = context.multiStepAggregator();

		// Act:
		auto shortHashes = aggregator.view().shortHashes();

		// Assert:
		EXPECT_TRUE(shortHashes.empty());
	}

	TEST(TEST_CLASS, ShortHashesReturnsShortHashesForAllMessages) {
		// Arrange:
		MessagesBuilder messagesBuilder;
		messagesBuilder.push({ 6, 4, 5 }, 100);
		messagesBuilder.push({ 6, 2, 5 }, 200);
		messagesBuilder.push({ 6, 8, 5 }, 300);
		messagesBuilder.push({ 6, 4, 5 }, 400);
		messagesBuilder.push({ 6, 2, 5 }, 500);
		messagesBuilder.push({ 6, 8, 5 }, 600);

		auto messageShortHashes = ToShortHashes(messagesBuilder);
		auto messageShortHashesSet = utils::ShortHashesSet(messageShortHashes.cbegin(), messageShortHashes.cend());

		TestContext context(2000, 3000, messagesBuilder.createProcessor());
		auto& aggregator = context.multiStepAggregator();

		ProcessTraits::AddAll(aggregator, FP(6), messagesBuilder);

		// Act:
		auto shortHashes = aggregator.view().shortHashes();

		// Assert:
		EXPECT_EQ(6u, shortHashes.size());

		// - cannot check shortHashes exactly because there's no sorting for messages within a step
		for (auto shortHash : shortHashes)
			EXPECT_CONTAINS(messageShortHashesSet, shortHash);
	}

	// endregion

	// region unknownMessages

	namespace {
		auto ToShortHashes(const std::vector<std::shared_ptr<const model::FinalizationMessage>>& messages) {
			utils::ShortHashesSet shortHashes;
			for (const auto& pMessage : messages)
				shortHashes.insert(utils::ToShortHash(model::CalculateMessageHash(*pMessage)));

			return shortHashes;
		}

		template<typename TAction>
		void RunUnknownMessagesTest(TAction action) {
			// Arrange:
			MessagesBuilder messagesBuilder;
			messagesBuilder.push({ 6, 4, 5 }, 100);
			messagesBuilder.push({ 6, 2, 5 }, 200);
			messagesBuilder.push({ 6, 8, 5 }, 300);
			messagesBuilder.push({ 6, 4, 5 }, 400);
			messagesBuilder.push({ 6, 2, 5 }, 500);
			messagesBuilder.push({ 6, 8, 5 }, 600);

			auto shortHashes = ToShortHashes(messagesBuilder);

			TestContext context(2000, 3000, messagesBuilder.createProcessor());
			auto& aggregator = context.multiStepAggregator();

			ProcessTraits::AddAll(aggregator, FP(6), messagesBuilder);

			// Act + Assert:
			action(aggregator, shortHashes);
		}
	}

	TEST(TEST_CLASS, UnknownMessagesReturnsNoMessagesWhenAggregatorIsEmpty) {
		// Arrange:
		MessagesBuilder messagesBuilder;

		TestContext context(2000, 3000, messagesBuilder.createProcessor());
		auto& aggregator = context.multiStepAggregator();

		// Act:
		auto unknownMessages = aggregator.view().unknownMessages({ 6, 0, 0 }, {});

		// Assert:
		EXPECT_TRUE(unknownMessages.empty());
	}

	TEST(TEST_CLASS, UnknownMessagesReturnsAllMessagesWhenFilterIsEmpty) {
		// Arrange:
		RunUnknownMessagesTest([](const auto& aggregator, const auto& shortHashes) {
			// Act:
			auto unknownMessages = aggregator.view().unknownMessages({ 6, 0, 0 }, {});

			// Assert:
			EXPECT_EQ(6u, unknownMessages.size());
			EXPECT_EQ(utils::ShortHashesSet(shortHashes.cbegin(), shortHashes.cend()), ToShortHashes(unknownMessages));
		});
	}

	TEST(TEST_CLASS, UnknownMessagesReturnsAllMessagesNotInFilter) {
		// Arrange:
		RunUnknownMessagesTest([](const auto& aggregator, const auto& shortHashes) {
			// Act:
			auto unknownMessages = aggregator.view().unknownMessages({ 6, 0, 0 }, { shortHashes[0], shortHashes[1], shortHashes[4] });

			// Assert:
			EXPECT_EQ(3u, unknownMessages.size());
			EXPECT_EQ(utils::ShortHashesSet({ shortHashes[2], shortHashes[3], shortHashes[5] }), ToShortHashes(unknownMessages));
		});
	}

	TEST(TEST_CLASS, UnknownMessagesReturnsNoMessagesWhenAllMessagesAreKnown) {
		// Arrange:
		RunUnknownMessagesTest([](const auto& aggregator, const auto& shortHashes) {
			// Act:
			auto shortHashesSet = utils::ShortHashesSet(shortHashes.cbegin(), shortHashes.cend());
			auto unknownMessages = aggregator.view().unknownMessages({ 6, 0, 0 }, shortHashesSet);

			// Assert:
			EXPECT_TRUE(unknownMessages.empty());
		});
	}

	TEST(TEST_CLASS, UnknownMessagesReturnsAllMessagesWithStepIdentifierNoLessThanFilterParameter) {
		// Arrange:
		RunUnknownMessagesTest([](const auto& aggregator, const auto& shortHashes) {
			// Act:
			auto unknownMessages = aggregator.view().unknownMessages({ 6, 4, 5 }, {});

			// Assert:
			EXPECT_EQ(4u, unknownMessages.size());
			EXPECT_EQ(
					utils::ShortHashesSet({ shortHashes[0], shortHashes[2], shortHashes[3], shortHashes[5] }),
					ToShortHashes(unknownMessages));
		});
	}

	namespace {
		template<typename TAction>
		void RunMaxResponseSizeTests(TAction action) {
			// Arrange: determine message size from a generated message
			MessagesBuilder messagesBuilder;
			messagesBuilder.push({ 6, 4, 5 }, 100);
			auto messageSize = messagesBuilder.message(0)->Size;

			// Assert:
			action(2, 3 * messageSize - 1);
			action(3, 3 * messageSize);
			action(3, 3 * messageSize + 1);

			action(3, 4 * messageSize - 1);
			action(4, 4 * messageSize);
		}
	}

	TEST(TEST_CLASS, UnknownMessagesReturnsMessagesWithTotalSizeOfAtMostMaxResponseSize_AcrossSteps) {
		// Arrange:
		RunMaxResponseSizeTests([](auto numExpectedMessages, auto maxResponseSize) {
			MessagesBuilder messagesBuilder;
			messagesBuilder.push({ 6, 2, 1 }, 100);
			messagesBuilder.push({ 6, 2, 2 }, 200);
			messagesBuilder.push({ 6, 4, 3 }, 300);
			messagesBuilder.push({ 6, 4, 4 }, 400);
			messagesBuilder.push({ 6, 8, 5 }, 500);
			messagesBuilder.push({ 6, 8, 6 }, 600);

			auto shortHashes = ToShortHashes(messagesBuilder);

			TestContextOptions options;
			options.MaxResponseSize = maxResponseSize;
			TestContext context(2000, 3000, options, messagesBuilder.createProcessor());
			auto& aggregator = context.multiStepAggregator();

			ProcessTraits::AddAll(aggregator, FP(6), messagesBuilder);

			// Act:
			auto unknownMessages = aggregator.view().unknownMessages({ 6, 0, 0 }, {});

			// Assert:
			EXPECT_EQ(numExpectedMessages, unknownMessages.size());
			EXPECT_EQ(
					utils::ShortHashesSet(shortHashes.cbegin(), shortHashes.cbegin() + numExpectedMessages),
					ToShortHashes(unknownMessages));
		});
	}

	TEST(TEST_CLASS, UnknownMessagesReturnsMessagesWithTotalSizeOfAtMostMaxResponseSize_WithinStep) {
		// Arrange:
		RunMaxResponseSizeTests([](auto numExpectedMessages, auto maxResponseSize) {
			MessagesBuilder messagesBuilder;
			for (auto numVotes : std::initializer_list<uint64_t>{ 100, 200, 300, 400, 500, 600 })
				messagesBuilder.push({ 6, 4, 5 }, numVotes);

			auto shortHashes = ToShortHashes(messagesBuilder);
			auto shortHashesSet = utils::ShortHashesSet(shortHashes.cbegin(), shortHashes.cend());

			TestContextOptions options;
			options.MaxResponseSize = maxResponseSize;
			TestContext context(2000, 3000, options, messagesBuilder.createProcessor());
			auto& aggregator = context.multiStepAggregator();

			ProcessTraits::AddAll(aggregator, FP(6), messagesBuilder);

			// Act:
			auto unknownMessages = aggregator.view().unknownMessages({ 6, 0, 0 }, {});

			// Assert:
			EXPECT_EQ(numExpectedMessages, unknownMessages.size());

			// - cannot check unknownMessages exactly because there's no sorting for messages within a step
			for (auto shortHash : ToShortHashes(unknownMessages))
				EXPECT_CONTAINS(shortHashesSet, shortHash);
		});
	}

	// endregion

	// region synchronization

	namespace {
		auto CreateLockProvider() {
			return std::make_unique<MultiStepFinalizationMessageAggregator>(
					10'000,
					MessageProcessor(),
					SingleStepAggregatorFactory(),
					ConsensusSink());
		}
	}

	DEFINE_LOCK_PROVIDER_TESTS(TEST_CLASS)

	// endregion
}}
