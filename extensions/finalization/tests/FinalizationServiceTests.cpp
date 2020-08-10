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

#include "finalization/src/FinalizationService.h"
#include "finalization/src/FinalizationBootstrapperService.h"
#include "tests/test/core/PacketTestUtils.h"
#include "tests/test/local/PacketWritersServiceTestUtils.h"
#include "tests/test/local/ServiceTestUtils.h"
#include "tests/test/net/RemoteAcceptServer.h"
#include "tests/test/net/mocks/MockPacketWriters.h"
#include "tests/TestHarness.h"

namespace catapult { namespace finalization {

#define TEST_CLASS FinalizationServiceTests

	namespace {
		struct FinalizationServiceTraits {
			static constexpr auto Counter_Name = "FIN WRITERS";
			static constexpr auto Num_Expected_Services = 2u; // writers (1) + dependent services (1)
			static constexpr auto CreateRegistrar = CreateFinalizationServiceRegistrar;

			static auto GetWriters(const extensions::ServiceLocator& locator) {
				return locator.service<net::PacketWriters>("fin.writers");
			}
		};

		class TestContext : public test::ServiceLocatorTestContext<FinalizationServiceTraits> {
		public:
			TestContext() {
				// Arrange: register service dependencies
				auto pBootstrapperRegistrar = CreateFinalizationBootstrapperServiceRegistrar();
				pBootstrapperRegistrar->registerServices(locator(), testState().state());

				// - register hook dependencies
				GetFinalizationServerHooks(locator()).setMessageRangeConsumer([](auto&&) {});
			}
		};

		struct Mixin {
			using TraitsType = FinalizationServiceTraits;
			using TestContextType = TestContext;
		};
	}

	ADD_SERVICE_REGISTRAR_INFO_TEST(Finalization, Post_Extended_Range_Consumers)

	ADD_PACKET_WRITERS_SERVICE_TEST(TEST_CLASS, Mixin, CanBootService)
	ADD_PACKET_WRITERS_SERVICE_TEST(TEST_CLASS, Mixin, CanShutdownService)
	ADD_PACKET_WRITERS_SERVICE_TEST(TEST_CLASS, Mixin, CanConnectToExternalServer)
	ADD_PACKET_WRITERS_SERVICE_TEST(TEST_CLASS, Mixin, WritersAreRegisteredInBannedNodeIdentitySink)

	// region packetIoPickers

	TEST(TEST_CLASS, WritersAreRegisteredInPacketIoPickers) {
		// Arrange: create a (tcp) server
		test::RemoteAcceptServer server;
		server.start();

		// Act: create and boot the service
		TestContext context;
		context.boot();
		auto pickers = context.testState().state().packetIoPickers();

		// - get the packet writers and attempt to connect to the server
		test::ConnectToLocalHost(*FinalizationServiceTraits::GetWriters(context.locator()), server.caPublicKey());

		// Assert: the writers are registered with role `Voting`
		EXPECT_EQ(0u, pickers.pickMatching(utils::TimeSpan::FromSeconds(1), ionet::NodeRoles::Peer).size());
		EXPECT_EQ(1u, pickers.pickMatching(utils::TimeSpan::FromSeconds(1), ionet::NodeRoles::Voting).size());
	}

	// endregion

	// region tasks

	TEST(TEST_CLASS, TasksAreRegistered) {
		test::AssertRegisteredTasks(TestContext(), {
			"connect peers task for service Finalization",
			"pull finalization messages task"
		});
	}

	// endregion
}}