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
#include "finalization/src/io/ProofStorage.h"

namespace catapult { namespace mocks {

	/// Mock proof storage.
	class MockProofStorage : public io::ProofStorage {
	public:
		/// Describes a saved proof.
		struct SavedProofDescriptor {
			/// Proof height.
			catapult::Height Height;

			/// Proof step identifier.
			crypto::StepIdentifier StepIdentifier;
		};

	public:
		/// Creates a proof storage initialized with a nemesis proof.
		MockProofStorage() {
			setLastFinalization(FinalizationPoint(1), Height(1));
		}

	public:
		/// Gets all saved proof descriptors.
		const auto& savedProofDescriptors() const {
			return m_savedProofDescriptors;
		}

	public:
		/// Sets the last finalization \a point and \a height.
		void setLastFinalization(FinalizationPoint point, Height height) {
			m_point = point;
			m_height = height;
		}

	public:
		FinalizationPoint finalizationPoint() const override {
			return m_point;
		}

		Height finalizedHeight() const override {
			return m_height;
		}

		model::HeightHashPairRange loadFinalizedHashesFrom(FinalizationPoint, size_t) const override {
			CATAPULT_THROW_RUNTIME_ERROR("loadFinalizedHashesFrom - not supported in mock");
		}

		std::shared_ptr<const model::PackedFinalizationProof> loadProof(FinalizationPoint) const override {
			CATAPULT_THROW_RUNTIME_ERROR("loadProof - not supported in mock");
		}

		void saveProof(Height height, const chain::FinalizationProof& proof) override {
			auto descriptor = SavedProofDescriptor{ height, proof.empty() ? crypto::StepIdentifier() : proof[0]->StepIdentifier };
			m_savedProofDescriptors.push_back(descriptor);
		}

	private:
		FinalizationPoint m_point;
		Height m_height;

		std::vector<SavedProofDescriptor> m_savedProofDescriptors;
	};
}}
