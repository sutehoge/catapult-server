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

#include "FinalizationContextFactory.h"
#include "catapult/cache/CatapultCache.h"
#include "catapult/cache_core/AccountStateCache.h"
#include "catapult/extensions/ServiceState.h"
#include "catapult/io/BlockStorageCache.h"

namespace catapult { namespace finalization {

	FinalizationContextFactory::FinalizationContextFactory(const FinalizationConfiguration& config, const extensions::ServiceState& state)
			: m_config(config)
			, m_accountStateCache(state.cache().sub<cache::AccountStateCache>())
			, m_blockStorage(state.storage())
	{}

	model::FinalizationContext FinalizationContextFactory::create(const model::FinalizationRound& round) const {
		// TODO: calculate height from round.Epoch
		auto votingSetHeight = FinalizationEpoch(1) >= round.Epoch
				? Height(1)
				: Height((round.Epoch.unwrap() - 1) * m_config.VotingSetGrouping);
		return model::FinalizationContext(
				round.Point, // TODO: this can be epoch
				votingSetHeight,
				m_blockStorage.view().loadBlockElement(votingSetHeight)->GenerationHash,
				m_config,
				*m_accountStateCache.createView());
	}
}}
