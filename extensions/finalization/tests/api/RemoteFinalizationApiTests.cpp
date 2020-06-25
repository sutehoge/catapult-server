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

#include "finalization/src/api/RemoteFinalizationApi.h"
#include "finalization/src/model/FinalizationMessage.h"
#include "tests/test/other/RemoteApiFactory.h"
#include "tests/test/other/RemoteApiTestUtils.h"
#include "tests/TestHarness.h"

namespace catapult { namespace api {

	namespace {
		std::shared_ptr<ionet::Packet> CreatePacketWithMessages(uint16_t numMessages) {
			// Arrange: create messages with variable (incrementing) sizes
			uint32_t variableDataSize = numMessages * (numMessages + 1) / 2;
			uint32_t payloadSize = numMessages * sizeof(model::FinalizationMessage) + variableDataSize * Hash256::Size;
			auto pPacket = ionet::CreateSharedPacket<ionet::Packet>(payloadSize);
			test::FillWithRandomData({ pPacket->Data(), payloadSize });

			auto pData = pPacket->Data();
			for (uint16_t i = 0u; i < numMessages; ++i) {
				auto& message = reinterpret_cast<model::FinalizationMessage&>(*pData);
				message.Size = sizeof(model::FinalizationMessage) + (i + 1) * Hash256::Size;
				message.HashesCount = i + 1;

				pData += message.Size;
			}

			return pPacket;
		}

		struct MessagesTraits {
			static constexpr uint32_t Request_Data_Header_Size = sizeof(crypto::StepIdentifier);
			static constexpr uint32_t Request_Data_Size = 3 * sizeof(utils::ShortHash);

			static std::vector<uint32_t> KnownShortHashValues() {
				return { 123, 234, 345 };
			}

			static model::ShortHashRange KnownShortHashes() {
				return model::ShortHashRange::CopyFixed(reinterpret_cast<uint8_t*>(KnownShortHashValues().data()), 3);
			}

			static auto Invoke(const RemoteFinalizationApi& api) {
				return api.messages({ 11, 22, 33 }, KnownShortHashes());
			}

			static auto CreateValidResponsePacket() {
				auto pResponsePacket = CreatePacketWithMessages(3);
				pResponsePacket->Type = ionet::PacketType::Pull_Finalization_Messages;
				return pResponsePacket;
			}

			static auto CreateMalformedResponsePacket() {
				// the packet is malformed because it contains a partial message
				auto pResponsePacket = CreateValidResponsePacket();
				--pResponsePacket->Size;
				return pResponsePacket;
			}

			static void ValidateRequest(const ionet::Packet& packet) {
				EXPECT_EQ(ionet::PacketType::Pull_Finalization_Messages, packet.Type);
				ASSERT_EQ(sizeof(ionet::Packet) + Request_Data_Header_Size + Request_Data_Size, packet.Size);
				EXPECT_EQ(crypto::StepIdentifier({ 11, 22, 33 }), reinterpret_cast<const crypto::StepIdentifier&>(*packet.Data()));
				EXPECT_EQ_MEMORY(packet.Data() + Request_Data_Header_Size, KnownShortHashValues().data(), Request_Data_Size);
			}

			static void ValidateResponse(const ionet::Packet& response, const FinalizationMessageRange& messages) {
				ASSERT_EQ(3u, messages.size());

				const auto* pExpectedData = response.Data();
				auto parsedIter = messages.cbegin();
				for (auto i = 0u; i < messages.size(); ++i, ++parsedIter) {
					std::string description = "comparing message at " + std::to_string(i);
					const auto& actualMessage = *parsedIter;

					// `response` is the (unprocessed) response Packet, which contains unaligned data
					// `messages` is the (processed) result, which is aligned
					std::vector<uint8_t> expectedMessageBuffer(actualMessage.Size);
					std::memcpy(&expectedMessageBuffer[0], pExpectedData, actualMessage.Size);
					const auto& expectedMessage = reinterpret_cast<const model::FinalizationMessage&>(expectedMessageBuffer[0]);

					ASSERT_EQ(expectedMessage.Size, actualMessage.Size) << description;
					EXPECT_EQ(i + 1, actualMessage.HashesCount) << description;
					EXPECT_EQ_MEMORY(&expectedMessage, &actualMessage, expectedMessage.Size) << description;

					pExpectedData += expectedMessage.Size;
				}
			}
		};

		struct RemoteFinalizationApiTraits {
			static auto Create(ionet::PacketIo& packetIo, const model::NodeIdentity& remoteIdentity) {
				return CreateRemoteFinalizationApi(packetIo, remoteIdentity);
			}

			static auto Create(ionet::PacketIo& packetIo) {
				return Create(packetIo, model::NodeIdentity());
			}
		};
	}

	DEFINE_REMOTE_API_TESTS(RemoteFinalizationApi)
	DEFINE_REMOTE_API_TESTS_EMPTY_RESPONSE_VALID(RemoteFinalizationApi, Messages)
}}
