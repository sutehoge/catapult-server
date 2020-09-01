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

#include "finalization/src/model/StepIdentifier.h"
#include "tests/test/nodeps/Alignment.h"
#include "tests/test/nodeps/Comparison.h"
#include "tests/test/nodeps/Functional.h"

namespace catapult { namespace model {

#define TEST_CLASS StepIdentifierTests

	// region finalization round operators

	namespace {
		std::vector<FinalizationRound> GenerateIncreasingFinalizationRoundValues() {
			return {
				{ FinalizationEpoch(7), FinalizationPoint(5) },
				{ FinalizationEpoch(7), FinalizationPoint(10) },
				{ FinalizationEpoch(7), FinalizationPoint(11) },
				{ FinalizationEpoch(8), FinalizationPoint(11) }
			};
		}
	}

	DEFINE_EQUALITY_AND_COMPARISON_TESTS_WITH_PREFIX(TEST_CLASS, GenerateIncreasingFinalizationRoundValues(), RoundIdentifier_)

	TEST(TEST_CLASS, FinalizationRoundCanOutput) {
		// Arrange:
		auto round = FinalizationRound{ FinalizationEpoch(7), FinalizationPoint(11) };

		// Act:
		auto str = test::ToString(round);

		// Assert:
		EXPECT_EQ("(7, 11)", str);
	}

	// endregion

	// region finalization round size + alignment

#define FINALIZATION_ROUND_FIELDS FIELD(Epoch) FIELD(Point)

	TEST(TEST_CLASS, FinalizationRoundHasExpectedSize) {
		// Arrange:
		auto expectedSize = 0u;

#define FIELD(X) expectedSize += SizeOf32<decltype(FinalizationRound::X)>();
		FINALIZATION_ROUND_FIELDS
#undef FIELD

		// Assert:
		EXPECT_EQ(expectedSize, sizeof(FinalizationRound));
		EXPECT_EQ(16u, sizeof(FinalizationRound));
	}

	TEST(TEST_CLASS, FinalizationRoundHasProperAlignment) {
#define FIELD(X) EXPECT_ALIGNED(FinalizationRound, X);
		FINALIZATION_ROUND_FIELDS
#undef FIELD

		EXPECT_EQ(0u, sizeof(FinalizationRound) % 8);
	}

#undef FINALIZATION_ROUND_FIELDS

	// endregion

	// region step identifier operators

	namespace {
		std::vector<StepIdentifier> GenerateIncreasingStepIdentifierValues() {
			return {
				{ FinalizationEpoch(7), FinalizationPoint(5), FinalizationStage::Prevote },
				{ FinalizationEpoch(7), FinalizationPoint(10), FinalizationStage::Prevote },
				{ FinalizationEpoch(7), FinalizationPoint(11), FinalizationStage::Prevote },
				{ FinalizationEpoch(7), FinalizationPoint(11), FinalizationStage::Precommit },
				{ FinalizationEpoch(7), FinalizationPoint(11), static_cast<FinalizationStage>(4) },
				{ FinalizationEpoch(8), FinalizationPoint(11), FinalizationStage::Prevote },
				{ FinalizationEpoch(8), FinalizationPoint(11), FinalizationStage::Precommit}
			};
		}
	}

	DEFINE_EQUALITY_AND_COMPARISON_TESTS_WITH_PREFIX(TEST_CLASS, GenerateIncreasingStepIdentifierValues(), StepIdentifier_)

	TEST(TEST_CLASS, StepIdentifierCanOutput) {
		// Arrange:
		auto stepIdentifier = StepIdentifier{ FinalizationEpoch(7), FinalizationPoint(11), static_cast<FinalizationStage>(5) };

		// Act:
		auto str = test::ToString(stepIdentifier);

		// Assert:
		EXPECT_EQ("(7, 11, 5)", str);
	}

	// endregion

	// region step identifier size + alignment

#define STEP_IDENTIFIER_FIELDS FIELD(Epoch) FIELD(Point) FIELD(Stage)

	TEST(TEST_CLASS, StepIdentifierHasExpectedSize) {
		// Arrange:
		auto expectedSize = 0u;

#define FIELD(X) expectedSize += SizeOf32<decltype(StepIdentifier::X)>();
		STEP_IDENTIFIER_FIELDS
#undef FIELD

		// Assert:
		EXPECT_EQ(expectedSize, sizeof(StepIdentifier));
		EXPECT_EQ(24u, sizeof(StepIdentifier));
	}

	TEST(TEST_CLASS, StepIdentifierHasProperAlignment) {
#define FIELD(X) EXPECT_ALIGNED(StepIdentifier, X);
		STEP_IDENTIFIER_FIELDS
#undef FIELD

		EXPECT_EQ(0u, sizeof(StepIdentifier) % 8);
	}

#undef STEP_IDENTIFIER_FIELDS

	// endregion

	// region StepIdentifierToOtsKeyIdentifier

	namespace {
		std::vector<StepIdentifier> GenerateValidStepIdentifierValues() {
			return {
				{ FinalizationEpoch(), FinalizationPoint(5), FinalizationStage::Prevote },
				{ FinalizationEpoch(), FinalizationPoint(10), FinalizationStage::Prevote },
				{ FinalizationEpoch(), FinalizationPoint(10), FinalizationStage::Precommit },
				{ FinalizationEpoch(), FinalizationPoint(11), FinalizationStage::Prevote },
				{ FinalizationEpoch(), FinalizationPoint(11), FinalizationStage::Precommit }
			};
		}
	}

	TEST(TEST_CLASS, StepIdentifierToOtsKeyIdentifierProducesCorrectValues) {
		// Arrange:
		auto identifiers = GenerateValidStepIdentifierValues();
		auto expectedKeyIdentifiers = std::vector<crypto::OtsKeyIdentifier>{
			{ 1, 3 }, { 2, 6 }, { 3, 0 }, { 3, 1 }, { 3, 2 }
		};

		// Act:
		auto keyIdentifiers = test::Apply(true, identifiers, [](const auto& stepIdentifier) {
			return StepIdentifierToOtsKeyIdentifier(stepIdentifier, 7);
		});

		// Assert:
		EXPECT_EQ(expectedKeyIdentifiers, keyIdentifiers);
	}

	TEST(TEST_CLASS, StepIdentifierToOtsKeyIdentifierProducesConflictingValuesForInvalidStepIdentifiers) {
		// Arrange: invalid, because round is greater than number of stages
		auto validIdentifier = StepIdentifier{ FinalizationEpoch(), FinalizationPoint(10), FinalizationStage::Precommit };
		auto invalidIdentifier = StepIdentifier{ FinalizationEpoch(), FinalizationPoint(8), static_cast<FinalizationStage>(5) };

		// Act:
		auto validKeyIdentifier = StepIdentifierToOtsKeyIdentifier(validIdentifier, 7);
		auto invalidKeyIdentifier = StepIdentifierToOtsKeyIdentifier(invalidIdentifier, 7);

		// Assert:
		EXPECT_EQ(validKeyIdentifier, invalidKeyIdentifier);
	}

	TEST(TEST_CLASS, StepIdentifierToOtsKeyIdentifierProducesCorrectValuesWhenDilutionIsOne) {
		// Arrange:
		auto identifiers = GenerateValidStepIdentifierValues();
		auto expectedKeyIdentifiers = std::vector<crypto::OtsKeyIdentifier>{
			{ 10, 0 }, { 20, 0 }, { 21, 0 }, { 22, 0 }, { 23, 0 }
		};

		// Act:
		auto keyIdentifiers = test::Apply(true, identifiers, [](const auto& stepIdentifier) {
			return StepIdentifierToOtsKeyIdentifier(stepIdentifier, 1);
		});

		// Assert:
		EXPECT_EQ(expectedKeyIdentifiers, keyIdentifiers);
	}

	// endregion
}}
