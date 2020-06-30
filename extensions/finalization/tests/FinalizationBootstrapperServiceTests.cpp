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

#include "finalization/src/FinalizationBootstrapperService.h"
#include "finalization/src/FinalizationConfiguration.h"
#include "finalization/src/chain/MultiStepFinalizationMessageAggregator.h"
#include "finalization/src/io/ProofStorageCache.h"
#include "finalization/tests/test/FinalizationBootstrapperServiceTestUtils.h"
#include "finalization/tests/test/mocks/MockProofStorage.h"
#include "tests/test/core/BlockTestUtils.h"
#include "tests/test/local/ServiceTestUtils.h"
#include "tests/test/nodeps/Filesystem.h"
#include "tests/TestHarness.h"

namespace catapult { namespace finalization {

#define TEST_CLASS FinalizationBootstrapperServiceTests

	// region FinalizationServerHooks

	namespace {
		struct MessageRangeConsumerTraits {
			using ParamType = model::AnnotatedEntityRange<model::FinalizationMessage>;

			static auto Get(const FinalizationServerHooks& hooks) {
				return hooks.messageRangeConsumer();
			}

			static void Set(FinalizationServerHooks& hooks, const handlers::RangeHandler<model::FinalizationMessage>& consumer) {
				hooks.setMessageRangeConsumer(consumer);
			}
		};
	}

#define CONSUMER_HOOK_TEST_ENTRY(TEST_NAME, CONSUMER_FACTORY_NAME) \
	TEST(TEST_CLASS, Hooks_##TEST_NAME##_##CONSUMER_FACTORY_NAME) { \
		TRAITS_TEST_NAME(TEST_CLASS, TEST_NAME)<CONSUMER_FACTORY_NAME##Traits>(); \
	}

#define CONSUMER_HOOK_TEST(TEST_NAME) \
	template<typename TTraits> void TRAITS_TEST_NAME(TEST_CLASS, TEST_NAME)(); \
	CONSUMER_HOOK_TEST_ENTRY(TEST_NAME, MessageRangeConsumer) \
	template<typename TTraits> void TRAITS_TEST_NAME(TEST_CLASS, TEST_NAME)()

	CONSUMER_HOOK_TEST(CannotAccessWhenUnset) {
		// Arrange:
		FinalizationServerHooks hooks;

		// Act + Assert:
		EXPECT_THROW(TTraits::Get(hooks), catapult_invalid_argument);
	}

	CONSUMER_HOOK_TEST(CanSetOnce) {
		// Arrange:
		FinalizationServerHooks hooks;

		typename TTraits::ParamType seedParam;
		const auto* pSeedParam = &seedParam;
		std::vector<decltype(pSeedParam)> consumedParams;

		TTraits::Set(hooks, [&consumedParams](auto&& param) {
			consumedParams.push_back(&param);
		});

		// Act:
		auto factory = TTraits::Get(hooks);
		ASSERT_TRUE(!!factory);

		factory(std::move(seedParam));

		// Assert: the param created above should be passed (and moved) down
		ASSERT_EQ(1u, consumedParams.size());
		EXPECT_EQ(pSeedParam, consumedParams[0]);
	}

	CONSUMER_HOOK_TEST(CannotSetMultipleTimes) {
		// Arrange:
		FinalizationServerHooks hooks;
		TTraits::Set(hooks, [](auto&&) {});

		// Act + Assert:
		EXPECT_THROW(TTraits::Set(hooks, [](auto&&) {}), catapult_invalid_argument);
	}

	// endregion

	// region FinalizationBootstrapperService - test context

	namespace {
		using VoterType = test::FinalizationBootstrapperServiceTestUtils::VoterType;

		constexpr auto Num_Services = test::FinalizationBootstrapperServiceTestUtils::Num_Bootstrapper_Services;
		constexpr auto Active_Steps_Counter_Name = "FIN ACT STEPS";

		struct FinalizationBootstrapperServiceTraits {
			static auto CreateRegistrar(std::unique_ptr<io::ProofStorage>&& pProofStorage) {
				auto config = FinalizationConfiguration::Uninitialized();
				config.Size = 3000;
				config.Threshold = 2000;
				return CreateFinalizationBootstrapperServiceRegistrar(config, std::move(pProofStorage));
			}

			static auto CreateRegistrar() {
				return CreateRegistrar(std::make_unique<mocks::MockProofStorage>());
			}
		};

		using TestContext = test::VoterSeededCacheDependentServiceLocatorTestContext<FinalizationBootstrapperServiceTraits>;

		void AssertStepIdentifierCounters(const TestContext& context, uint64_t point, uint64_t round, uint64_t subround) {
			EXPECT_EQ(point, context.counter("FIN POINT"));
			EXPECT_EQ(round, context.counter("FIN ROUND"));
			EXPECT_EQ(subround, context.counter("FIN SUBROUND"));
		}
	}

	// endregion

	// region FinalizationBootstrapperService - basic

	ADD_SERVICE_REGISTRAR_INFO_TEST(FinalizationBootstrapper, Initial)

	TEST(TEST_CLASS, MultiStepAggregatorServiceIsRegistered) {
		// Arrange:
		TestContext context;

		// Act:
		context.boot();

		// Assert:
		EXPECT_EQ(Num_Services, context.locator().numServices());
		EXPECT_EQ(4u, context.locator().counters().size());

		// - service
		const auto& aggregator = GetMultiStepFinalizationMessageAggregator(context.locator());
		EXPECT_EQ(0u, aggregator.view().size());
		EXPECT_EQ(crypto::StepIdentifier({ 2, 0, 0 }), aggregator.view().minStepIdentifier());

		// - counters
		EXPECT_EQ(0u, context.counter(Active_Steps_Counter_Name));
		AssertStepIdentifierCounters(context, 2, 0, 0);
	}

	TEST(TEST_CLASS, FinalizationHooksServiceIsRegistered) {
		// Arrange:
		TestContext context;

		// Act:
		context.boot();

		// Assert:
		EXPECT_EQ(Num_Services, context.locator().numServices());

		// - service (get does not throw)
		GetFinalizationServerHooks(context.locator());
	}

	TEST(TEST_CLASS, ProofStorageServiceIsRegistered) {
		// Arrange:
		TestContext context;

		// Act:
		context.boot();

		// Assert:
		EXPECT_EQ(Num_Services, context.locator().numServices());

		// - service (get does not throw)
		context.locator().service<io::ProofStorageCache>("fin.proof.storage");
	}

	// endregion

	// region FinalizationBootstrapperService - multi step aggregator

	namespace {
		const auto& GetFinalizationSubscriberParams(const TestContext& context) {
			return context.testState().finalizationSubscriber().finalizedBlockParams().params();
		}
	}

	TEST(TEST_CLASS, MultiStepAggregatorServiceCountersAreNotUpdatedWhenMessageIsRejected) {
		// Arrange:
		auto pProofStorage = std::make_unique<mocks::MockProofStorage>();
		const auto& proofStorage = *pProofStorage;

		TestContext context;
		context.boot(std::move(pProofStorage));

		auto pMessage = context.createMessage(VoterType::Ineligible, { 2, 3, 4 }, test::GenerateRandomByteArray<Hash256>());

		// Act:
		auto& aggregator = GetMultiStepFinalizationMessageAggregator(context.locator());
		aggregator.modifier().add(pMessage);

		// - wait for message to be processed
		test::Pause();

		// Assert:
		EXPECT_EQ(0u, context.counter(Active_Steps_Counter_Name));
		AssertStepIdentifierCounters(context, 2, 0, 0);

		// - check aggregator
		EXPECT_EQ(crypto::StepIdentifier({ 2, 0, 0 }), aggregator.view().minStepIdentifier());

		// - subscriber and storage weren't called
		EXPECT_TRUE(GetFinalizationSubscriberParams(context).empty());
		EXPECT_TRUE(proofStorage.savedProofDescriptors().empty());
	}

	TEST(TEST_CLASS, MultiStepAggregatorServiceCountersAreUpdatedWhenMessageIsAccepted) {
		// Arrange:
		auto pProofStorage = std::make_unique<mocks::MockProofStorage>();
		const auto& proofStorage = *pProofStorage;

		TestContext context;
		context.boot(std::move(pProofStorage));

		auto pMessage = context.createMessage(VoterType::Large1, { 2, 3, 4 }, test::GenerateRandomByteArray<Hash256>());

		// Act:
		auto& aggregator = GetMultiStepFinalizationMessageAggregator(context.locator());
		aggregator.modifier().add(pMessage);

		// - wait for message to be processed
		WAIT_FOR_ONE_EXPR(context.counter(Active_Steps_Counter_Name));

		// Assert:
		EXPECT_EQ(1u, context.counter(Active_Steps_Counter_Name));
		AssertStepIdentifierCounters(context, 2, 0, 0);

		// - check aggregator
		EXPECT_EQ(crypto::StepIdentifier({ 2, 0, 0 }), aggregator.view().minStepIdentifier());

		// - subscriber and storage weren't called
		EXPECT_TRUE(GetFinalizationSubscriberParams(context).empty());
		EXPECT_TRUE(proofStorage.savedProofDescriptors().empty());
	}

	TEST(TEST_CLASS, MultiStepAggregatorServiceCountersAreUpdatedWhenMessageConsensusIsReached) {
		// Arrange:
		auto pProofStorage = std::make_unique<mocks::MockProofStorage>();
		const auto& proofStorage = *pProofStorage;

		TestContext context;
		context.boot(std::move(pProofStorage));

		auto hash = test::GenerateRandomByteArray<Hash256>();
		auto pMessage1 = context.createMessage(VoterType::Large1, { 2, 3, 4 }, hash);
		auto pMessage2 = context.createMessage(VoterType::Large2, { 2, 3, 4 }, hash);

		// Act:
		auto& aggregator = GetMultiStepFinalizationMessageAggregator(context.locator());
		aggregator.modifier().add(pMessage1);
		aggregator.modifier().add(pMessage2);

		// - wait for message to be processed
		WAIT_FOR_VALUE_EXPR(crypto::StepIdentifier({ 2, 3, 4 }), aggregator.view().minStepIdentifier());

		// Assert:
		EXPECT_EQ(1u, context.counter(Active_Steps_Counter_Name));
		AssertStepIdentifierCounters(context, 2, 3, 4);

		// - check aggregator
		EXPECT_EQ(crypto::StepIdentifier({ 2, 3, 4 }), aggregator.view().minStepIdentifier());

		// - subscriber was called
		const auto& subscriberParams = GetFinalizationSubscriberParams(context);
		ASSERT_EQ(1u, subscriberParams.size());
		EXPECT_EQ(Height(2), subscriberParams[0].Height);
		EXPECT_EQ(hash, subscriberParams[0].Hash);
		EXPECT_EQ(FinalizationPoint(2), subscriberParams[0].Point);

		// - storage was called
		const auto& savedProofDescriptors = proofStorage.savedProofDescriptors();
		ASSERT_EQ(1u, savedProofDescriptors.size());
		EXPECT_EQ(Height(2), savedProofDescriptors[0].Height);
		EXPECT_EQ(crypto::StepIdentifier({ 2, 3, 4 }), savedProofDescriptors[0].StepIdentifier);
	}

	TEST(TEST_CLASS, MultiStepAggregatorServiceCountersAreUpdatedWhenMessageConsensusIsReached_WhenLastFinalizedPointIsNotNemesis) {
		// Arrange:
		auto pProofStorage = std::make_unique<mocks::MockProofStorage>();
		pProofStorage->setLastFinalization(FinalizationPoint(2), Height(3));
		const auto& proofStorage = *pProofStorage;

		GenerationHash lastFinalizedGenerationHash;
		TestContext context;
		{
			auto pBlock3 = test::GenerateBlockWithTransactions(0, Height(3));
			auto blockElement3 = test::BlockToBlockElement(*pBlock3);
			lastFinalizedGenerationHash = blockElement3.GenerationHash;

			// - set height to 3 (the last finalized block)
			auto storageModifier = context.testState().state().storage().modifier();
			storageModifier.saveBlock(test::BlockToBlockElement(*test::GenerateBlockWithTransactions(0, Height(2))));
			storageModifier.saveBlock(blockElement3);
			storageModifier.commit();
		}

		context.boot(std::move(pProofStorage));

		auto hash = test::GenerateRandomByteArray<Hash256>();
		auto pMessage1 = context.createMessage(VoterType::Large1, { 3, 6, 9 }, Height(5), hash, lastFinalizedGenerationHash);
		auto pMessage2 = context.createMessage(VoterType::Large2, { 3, 6, 9 }, Height(5), hash, lastFinalizedGenerationHash);

		// Act:
		auto& aggregator = GetMultiStepFinalizationMessageAggregator(context.locator());
		aggregator.modifier().add(pMessage1);
		aggregator.modifier().add(pMessage2);

		// - wait for message to be processed
		WAIT_FOR_VALUE_EXPR(crypto::StepIdentifier({ 3, 6, 9 }), aggregator.view().minStepIdentifier());

		// Assert:
		EXPECT_EQ(1u, context.counter(Active_Steps_Counter_Name));
		AssertStepIdentifierCounters(context, 3, 6, 9);

		// - check aggregator
		EXPECT_EQ(crypto::StepIdentifier({ 3, 6, 9 }), aggregator.view().minStepIdentifier());

		// - subscriber was called
		const auto& subscriberParams = GetFinalizationSubscriberParams(context);
		ASSERT_EQ(1u, subscriberParams.size());
		EXPECT_EQ(Height(5), subscriberParams[0].Height);
		EXPECT_EQ(hash, subscriberParams[0].Hash);
		EXPECT_EQ(FinalizationPoint(3), subscriberParams[0].Point);

		// - storage was called
		const auto& savedProofDescriptors = proofStorage.savedProofDescriptors();
		ASSERT_EQ(1u, savedProofDescriptors.size());
		EXPECT_EQ(Height(5), savedProofDescriptors[0].Height);
		EXPECT_EQ(crypto::StepIdentifier({ 3, 6, 9 }), savedProofDescriptors[0].StepIdentifier);
	}

	// endregion
}}
