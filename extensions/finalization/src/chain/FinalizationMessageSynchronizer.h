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

#pragma once
#include "finalization/src/handlers/FinalizationHandlerTypes.h"
#include "catapult/chain/RemoteNodeSynchronizer.h"
#include "catapult/model/RangeTypes.h"

namespace catapult { namespace api { class RemoteFinalizationApi; } }

namespace catapult { namespace chain {

	/// Function signature for supplying a step identifier.
	using StepIdentifierSupplier = supplier<crypto::StepIdentifier>;

	/// Function signature for supplying a range of short hashes.
	using ShortHashesSupplier = supplier<model::ShortHashRange>;

	/// Creates a finalization message synchronizer around the specified step identifier supplier (\a stepIdentifierSupplier),
	/// short hashes supplier (\a shortHashesSupplier) and message range consumer (\a messageRangeConsumer).
	RemoteNodeSynchronizer<api::RemoteFinalizationApi> CreateFinalizationMessageSynchronizer(
			const StepIdentifierSupplier& stepIdentifierSupplier,
			const ShortHashesSupplier& shortHashesSupplier,
			const handlers::MessageRangeHandler& messageRangeConsumer);
}}
