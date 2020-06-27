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
#include "finalization/src/model/FinalizationMessage.h"
#include "catapult/extensions/BasicServerHooks.h"
#include "catapult/extensions/ServiceRegistrar.h"
#include "catapult/handlers/HandlerTypes.h"

namespace catapult {
	namespace chain { class MultiStepFinalizationMessageAggregator; }
	namespace finalization { struct FinalizationConfiguration; }
}

namespace catapult { namespace finalization {

	// region FinalizationServerHooks

	/// Hooks for the finalization subsystem.
	class FinalizationServerHooks {
	public:
		/// Sets the message range \a consumer.
		void setMessageRangeConsumer(const handlers::RangeHandler<model::FinalizationMessage>& consumer) {
			extensions::SetOnce(m_messageRangeConsumer, consumer);
		}

	public:
		/// Gets the message range consumer.
		const auto& messageRangeConsumer() const {
			return extensions::Require(m_messageRangeConsumer);
		}

	private:
		handlers::RangeHandler<model::FinalizationMessage> m_messageRangeConsumer;
	};

	// endregion

	/// Creates a registrar for a finalization bootstrapper service around \a config.
	/// \note This service is responsible for registering root finalization services.
	DECLARE_SERVICE_REGISTRAR(FinalizationBootstrapper)(const FinalizationConfiguration& config);

	/// Gets the multi step finalization message aggregator stored in \a locator.
	chain::MultiStepFinalizationMessageAggregator& GetMultiStepFinalizationMessageAggregator(const extensions::ServiceLocator& locator);

	/// Gets the finalization server hooks stored in \a locator.
	FinalizationServerHooks& GetFinalizationServerHooks(const extensions::ServiceLocator& locator);
}}
