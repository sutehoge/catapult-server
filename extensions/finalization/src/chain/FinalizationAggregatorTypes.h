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
#include "SingleStepFinalizationMessageAggregator.h"
#include "finalization/src/model/FinalizationMessage.h"
#include "catapult/functions.h"
#include <vector>

namespace catapult { namespace crypto { struct StepIdentifier; } }

namespace catapult { namespace chain {

	/// Finalization proof.
	using FinalizationProof = std::vector<std::shared_ptr<const model::FinalizationMessage>>;

	/// Processes a finalization message.
	using MessageProcessor = std::function<std::pair<model::ProcessMessageResult, size_t> (const model::FinalizationMessage&)>;

	/// Factory for creating a message aggregator given a step identifier.
	using SingleStepAggregatorFactory = std::function<
		std::unique_ptr<SingleStepFinalizationMessageAggregator> (const crypto::StepIdentifier&)
	>;

	/// Callback called when consensus is reached.
	using ConsensusSink = consumer<const crypto::StepIdentifier&, const Hash256&, const FinalizationProof&>;
}}
