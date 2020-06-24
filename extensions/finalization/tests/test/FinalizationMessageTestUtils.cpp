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

#include "FinalizationMessageTestUtils.h"
#include "tests/test/nodeps/Random.h"

namespace catapult { namespace test {

	std::unique_ptr<model::FinalizationMessage> CreateMessage(const Hash256& hash) {
		return CreateMessage({ test::Random(), test::Random(), test::Random() }, hash);
	}

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
}}
