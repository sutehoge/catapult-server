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

#include "FinalizationStageAdvancer.h"
#include "MultiRoundMessageAggregator.h"
#include "RoundContext.h"
#include "finalization/src/FinalizationConfiguration.h"
#include "catapult/model/HeightGrouping.h"

namespace catapult { namespace chain {

	namespace {
		class PollingTimer {
		public:
			PollingTimer(Timestamp startTime, const utils::TimeSpan& stepDuration)
					: m_startTime(startTime)
					, m_stepDuration(stepDuration)
			{}

		public:
			bool isElapsed(Timestamp time, uint16_t numSteps) const {
				return time >= m_startTime + Timestamp(numSteps * m_stepDuration.millis());
			}

		private:
			Timestamp m_startTime;
			utils::TimeSpan m_stepDuration;
		};

		bool IsVotingSetEndHeight(Height height, uint64_t votingSetGrouping) {
			auto votingSetHeight = model::CalculateGroupedHeight<Height>(height, votingSetGrouping);
			auto nextVotingSetHeight = model::CalculateGroupedHeight<Height>(height + Height(1), votingSetGrouping);
			return votingSetHeight != nextVotingSetHeight;
		}

		class DefaultFinalizationStageAdvancer : public FinalizationStageAdvancer {
		public:
			DefaultFinalizationStageAdvancer(
					const finalization::FinalizationConfiguration& config,
					FinalizationPoint point,
					Timestamp time,
					const MultiRoundMessageAggregator& messageAggregator)
					: m_config(config)
					, m_point(point)
					, m_timer(time, m_config.StepDuration)
					, m_messageAggregator(messageAggregator)
			{}

		public:
			bool canSendPrevote(Timestamp time) const override {
				if (m_timer.isElapsed(time, 1))
					return true;

				return requireRoundContext([](const auto&, const auto& roundContext) {
					return roundContext.isCompletable();
				});
			}

			bool canSendPrecommit(Timestamp time, model::HeightHashPair& target) const override {
				return requireRoundContext([this, time, &target](const auto& messageAggregatorView, const auto& roundContext) {
					auto bestPrevoteResultPair = roundContext.tryFindBestPrevote();
					if (!bestPrevoteResultPair.second)
						return false;

					auto estimate = messageAggregatorView.findEstimate(m_point - FinalizationPoint(1));
					if (!roundContext.isDescendant(estimate, bestPrevoteResultPair.first))
						return false;

					if (!m_timer.isElapsed(time, 2) && !roundContext.isCompletable())
						return false;

					target = bestPrevoteResultPair.first;
					return true;
				});
			}

			bool canStartNextRound() const override {
				return requireRoundContext([this](const auto& messageAggregatorView, const auto& roundContext) {
					if (!roundContext.isCompletable())
						return false;

					// if the best estimate cannot end a voting set, the next round can start immediately
					// even if the best estimate is finalized, it will not end the voting set
					auto votingSetGrouping = m_config.VotingSetGrouping;
					auto estimate = messageAggregatorView.findEstimate(m_point);
					if (!IsVotingSetEndHeight(estimate.Height, votingSetGrouping))
						return true;

					// if the best precommit ends a voting set, the next round can start
					auto bestPrecommitResultPair = roundContext.tryFindBestPrecommit();
					return bestPrecommitResultPair.second && IsVotingSetEndHeight(bestPrecommitResultPair.first.Height, votingSetGrouping);
				});
			}

		private:
			bool requireRoundContext(const predicate<const MultiRoundMessageAggregatorView&, const RoundContext&>& predicate) const {
				auto messageAggregatorView = m_messageAggregator.view();

				const auto* pCurrentRoundContext = messageAggregatorView.tryGetRoundContext(m_point);
				if (!pCurrentRoundContext)
					return false;

				return predicate(messageAggregatorView, *pCurrentRoundContext);
			}

		private:
			finalization::FinalizationConfiguration m_config;
			FinalizationPoint m_point;
			PollingTimer m_timer;
			const MultiRoundMessageAggregator& m_messageAggregator;
		};
	}

	std::unique_ptr<FinalizationStageAdvancer> CreateFinalizationStageAdvancer(
			const finalization::FinalizationConfiguration& config,
			FinalizationPoint point,
			Timestamp time,
			const MultiRoundMessageAggregator& messageAggregator) {
		return std::make_unique<DefaultFinalizationStageAdvancer>(config, point, time, messageAggregator);
	}
}}
