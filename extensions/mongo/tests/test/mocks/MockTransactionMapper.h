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
#include "mongo/src/MongoTransactionPlugin.h"
#include "tests/test/core/mocks/MockTransaction.h"

namespace catapult { namespace mocks {

	/// Creates a mock mongo transaction plugin with the specified \a type.
	std::unique_ptr<mongo::MongoTransactionPlugin> CreateMockTransactionMongoPlugin(model::EntityType type);

	/// Creates a mock mongo transaction plugin with \a options and \a numDependentDocuments dependent documents.
	std::unique_ptr<mongo::MongoTransactionPlugin> CreateMockTransactionMongoPlugin(
			PluginOptionFlags options = PluginOptionFlags::Default,
			size_t numDependentDocuments = 0);
}}
