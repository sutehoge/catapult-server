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
#include "finalization/tests/test/FinalizationBootstrapperServiceTestUtils.h"
#include "tests/test/local/ServiceTestUtils.h"
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

		constexpr auto Active_Steps_Counter_Name = "FIN ACT STEPS";

		struct FinalizationBootstrapperServiceTraits {
			static auto CreateRegistrar() {
				auto config = FinalizationConfiguration::Uninitialized();
				config.Size = 3000;
				config.Threshold = 2000;
				return CreateFinalizationBootstrapperServiceRegistrar(config);
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
		EXPECT_EQ(2u, context.locator().numServices());
		EXPECT_EQ(4u, context.locator().counters().size());

		// - service
		const auto& aggregator = GetMultiStepFinalizationMessageAggregator(context.locator());
		EXPECT_EQ(0u, aggregator.view().size());
		EXPECT_EQ(crypto::StepIdentifier({ 1, 0, 0 }), aggregator.view().minStepIdentifier());

		// - counters
		EXPECT_EQ(0u, context.counter(Active_Steps_Counter_Name));
		AssertStepIdentifierCounters(context, 1, 0, 0);
	}

	TEST(TEST_CLASS, FinalizationHooksServiceIsRegistered) {
		// Arrange:
		TestContext context;

		// Act:
		context.boot();

		// Assert:
		EXPECT_EQ(2u, context.locator().numServices());

		// - service (get does not throw)
		GetFinalizationServerHooks(context.locator());
	}

	// endregion

	// region FinalizationBootstrapperService - multi step aggregator

	TEST(TEST_CLASS, MultiStepAggregatorServiceCountersAreNotUpdatedWhenMessageIsRejected) {
		// Arrange:
		TestContext context;
		context.boot();

		auto pMessage = context.createMessage(VoterType::Ineligible, { 1, 2, 3 }, test::GenerateRandomByteArray<Hash256>());

		// Act:
		auto& aggregator = GetMultiStepFinalizationMessageAggregator(context.locator());
		aggregator.modifier().add(pMessage);

		// - wait for message to be processed
		test::Pause();

		// Assert:
		EXPECT_EQ(0u, context.counter(Active_Steps_Counter_Name));
		AssertStepIdentifierCounters(context, 1, 0, 0);
	}

	TEST(TEST_CLASS, MultiStepAggregatorServiceCountersAreUpdatedWhenMessageIsAccepted) {
		// Arrange:
		TestContext context;
		context.boot();

		auto pMessage = context.createMessage(VoterType::Large1, { 1, 2, 3 }, test::GenerateRandomByteArray<Hash256>());

		// Act:
		auto& aggregator = GetMultiStepFinalizationMessageAggregator(context.locator());
		aggregator.modifier().add(pMessage);

		// - wait for message to be processed
		WAIT_FOR_ONE_EXPR(context.counter(Active_Steps_Counter_Name));

		// Assert:
		EXPECT_EQ(1u, context.counter(Active_Steps_Counter_Name));
		AssertStepIdentifierCounters(context, 1, 0, 0);

		// Sanity:
		EXPECT_EQ(crypto::StepIdentifier({ 1, 0, 0 }), aggregator.view().minStepIdentifier());
	}

	TEST(TEST_CLASS, MultiStepAggregatorServiceCountersAreUpdatedWhenMessageConsensusIsReached) {
		// Arrange:
		TestContext context;
		context.boot();

		auto hash = test::GenerateRandomByteArray<Hash256>();
		auto pMessage1 = context.createMessage(VoterType::Large1, { 1, 2, 3 }, hash);
		auto pMessage2 = context.createMessage(VoterType::Large2, { 1, 2, 3 }, hash);

		// Act:
		auto& aggregator = GetMultiStepFinalizationMessageAggregator(context.locator());
		aggregator.modifier().add(pMessage1);
		aggregator.modifier().add(pMessage2);

		// - wait for message to be processed
		WAIT_FOR_VALUE_EXPR(crypto::StepIdentifier({ 1, 2, 3 }), aggregator.view().minStepIdentifier());

		// Assert:
		EXPECT_EQ(1u, context.counter(Active_Steps_Counter_Name));
		AssertStepIdentifierCounters(context, 1, 2, 3);

		// Sanity:
		EXPECT_EQ(crypto::StepIdentifier({ 1, 2, 3 }), aggregator.view().minStepIdentifier());
	}

	// endregion
}}
