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

#include "FinalizationBootstrapperService.h"
#include "finalization/src/chain/MultiStepFinalizationMessageAggregator.h"
#include "finalization/src/model/FinalizationContext.h"
#include "catapult/cache/CatapultCache.h"
#include "catapult/cache_core/AccountStateCache.h"
#include "catapult/extensions/ConfigurationUtils.h"
#include "catapult/extensions/ServiceLocator.h"
#include "catapult/extensions/ServiceState.h"
#include "catapult/io/BlockStorageCache.h"

namespace catapult { namespace finalization {

	namespace {
		constexpr auto Hooks_Service_Name = "fin.hooks";
		constexpr auto Aggregator_Service_Name = "fin.aggregator.multistep";

		// region CreateMultiStepAggregator

		FinalizationPoint GetNextFinalizationPoint(const io::BlockStorageCache& /*storage*/) {
			// TODO: this is a placeholder
			return FinalizationPoint(1);
		}

		chain::SingleStepAggregatorFactory CreateSingleStepAggregatorFactory(const FinalizationConfiguration& config) {
			return [config](const auto& stepIdentifier) {
				CATAPULT_LOG(debug) << "creating single step aggregator for: " << stepIdentifier;
				// TODO: this will need to be step-dependent so we can process different types of messages
				return chain::CreateFinalizationMessageCountVotesAggregator(config);
			};
		}

		chain::MessageProcessor CreateFinalizationMessageProcessor(
				const FinalizationConfiguration& config,
				const cache::AccountStateCache& accountStateCache,
				const io::BlockStorageCache& storage) {
			return [config, &accountStateCache, &storage](const auto& message) {
				// TODO: point and height need to come from proof storage
				auto point = GetNextFinalizationPoint(storage);

				Height height;
				GenerationHash generationHash;
				{
					auto storageView = storage.view();
					height = Height(1);
					generationHash = storageView.loadBlockElement(height)->GenerationHash;
				}

				model::FinalizationContext finalizationContext(point, height, generationHash, config, *accountStateCache.createView());
				return ProcessMessage(message, finalizationContext);
			};
		}

		auto CreateMultiStepAggregator(const FinalizationConfiguration& config, extensions::ServiceState& state) {
			auto messageProcessor = CreateFinalizationMessageProcessor(
					config,
					state.cache().sub<cache::AccountStateCache>(),
					state.storage());
			return std::make_shared<chain::MultiStepFinalizationMessageAggregator>(
					config.MessageSynchronizationMaxResponseSize.bytes(),
					messageProcessor,
					CreateSingleStepAggregatorFactory(config),
					[](const auto& stepIdentifier, const auto&, const auto&) {
						CATAPULT_LOG(important) << "finalized consensus reached for " << stepIdentifier;
						// TODO: this should call subscriber
					});
		}

		// endregion

		// region FinalizationBootstrapperServiceRegistrar

		class FinalizationBootstrapperServiceRegistrar : public extensions::ServiceRegistrar {
		public:
			explicit FinalizationBootstrapperServiceRegistrar(const FinalizationConfiguration& config) : m_config(config)
			{}

		public:
			extensions::ServiceRegistrarInfo info() const override {
				return { "FinalizationBootstrapper", extensions::ServiceRegistrarPhase::Initial };
			}

			void registerServiceCounters(extensions::ServiceLocator& locator) override {
				using MultiStepAggregator = chain::MultiStepFinalizationMessageAggregator;

				locator.registerServiceCounter<MultiStepAggregator>(Aggregator_Service_Name, "FIN ACT STEPS", [](const auto& aggregator) {
					return aggregator.view().size();
				});
				locator.registerServiceCounter<MultiStepAggregator>(Aggregator_Service_Name, "FIN POINT", [](const auto& aggregator) {
					return aggregator.view().minStepIdentifier().Point;
				});
				locator.registerServiceCounter<MultiStepAggregator>(Aggregator_Service_Name, "FIN ROUND", [](const auto& aggregator) {
					return aggregator.view().minStepIdentifier().Round;
				});
				locator.registerServiceCounter<MultiStepAggregator>(Aggregator_Service_Name, "FIN SUBROUND", [](const auto& aggregator) {
					return aggregator.view().minStepIdentifier().SubRound;
				});
			}

			void registerServices(extensions::ServiceLocator& locator, extensions::ServiceState& state) override {
				// register services
				locator.registerRootedService(Hooks_Service_Name, std::make_shared<FinalizationServerHooks>());

				auto pMultiStepAggregator = CreateMultiStepAggregator(m_config, state);
				pMultiStepAggregator->modifier().setNextFinalizationPoint(FinalizationPoint(1)); // TODO: read from proof storage
				locator.registerRootedService(Aggregator_Service_Name, pMultiStepAggregator);
			}

		private:
			FinalizationConfiguration m_config;
		};

		// endregion
	}

	DECLARE_SERVICE_REGISTRAR(FinalizationBootstrapper)(const FinalizationConfiguration& config) {
		return std::make_unique<FinalizationBootstrapperServiceRegistrar>(config);
	}

	chain::MultiStepFinalizationMessageAggregator& GetMultiStepFinalizationMessageAggregator(const extensions::ServiceLocator& locator) {
		return *locator.service<chain::MultiStepFinalizationMessageAggregator>(Aggregator_Service_Name);
	}

	FinalizationServerHooks& GetFinalizationServerHooks(const extensions::ServiceLocator& locator) {
		return *locator.service<FinalizationServerHooks>(Hooks_Service_Name);
	}
}}
