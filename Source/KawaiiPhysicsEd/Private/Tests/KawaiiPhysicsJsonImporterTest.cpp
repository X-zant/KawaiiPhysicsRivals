// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License

#include "AnimNode_KawaiiPhysics.h"
#include "KawaiiPhysicsExternalForce.h"
#include "KawaiiPhysicsJsonImporter.h"

#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Covers the parts of the JSON import that carry real logic: enum qualifiers,
 * object references that cannot resolve, and runtime state that must not be
 * imported. The fixture mirrors the shape FModel actually emits for a cooked
 * Post_*_Physics AnimBlueprint (verified against
 * Marvel/Content/Marvel/Characters/1014/1014001/Post_1014001_Physics).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsJsonImporterTest,
                                 "KawaiiPhysics.JsonImporter.ParseNode",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsJsonImporterTest::RunTest(const FString& Parameters)
{
	const FString NodeJson = TEXT(R"JSON(
{
  "Chains": [
    {
      "BoneSettings": {
        "RootBone": { "BoneName": "Hair_001_D1" },
        "ExcludeBones": [],
        "bRootBoneSimulate": false,
        "bShouldFixTailBone": true,
        "FixedBone": { "BoneName": "None" },
        "FollowBone": { "BoneName": "None" },
        "AdditionalRootBones": [],
        "DummyBoneLength": 4.0,
        "BoneForwardAxis": "EBoneForwardAxis::Y_Negative"
      },
      "PhysicsSettings": {
        "PhysicsSettings": {
          "Damping": 0.35,
          "Stiffness": 0.08,
          "WorldDampingLocation": 0.98,
          "WorldDampingRotation": 0.95,
          "Radius": 3.0,
          "LimitAngle": 5.0,
          "LimitLinear": 0.0,
          "GravityScale": 1.0
        },
        "PlanarConstraint": "EPlanarConstraint::Z",
        "TeleportDistanceThreshold": 120.0
      },
      "BoneConstraintSettings": {
        "BoneConstraintGlobalComplianceType": "EXPBDComplianceType::Rubber",
        "BoneConstraintsDataAsset": {
          "ObjectName": "KawaiiPhysicsBoneConstraintsDataAsset'DA_Constraints'",
          "ObjectPath": "Marvel/Content/Whatever/DA_Constraints.0"
        }
      },
      "ExternalForceSettings": {
        "Gravity": { "X": 0.0, "Y": 0.0, "Z": -980.0 },
        "bEnableWind": true,
        "WindScale": 2.5,
        "ExternalForces": [
          {
            "ForceDir": { "X": 0.0, "Y": 0.0, "Z": -1.0 },
            "Interval": 0.0,
            "bIsEnabled": true,
            "ExternalForceSpace": "EExternalForceSpace::WorldSpace",
            "RandomForceScaleRange": { "Min": 10.0, "Max": 10.0 }
          }
        ]
      },
      "ModifyBones": [ { "Index": 0 }, { "Index": 1 } ],
      "AutoConfiguredLODThreshold": 2
    }
  ],
  "TargetFramerate": 30,
  "OverrideTargetFramerate": true,
  "bAllowWorldCollision": true,
  "SphericalLimits": [
    {
      "bUseParentCompBone": true,
      "DrivingBone": { "BoneName": "Spine_02" },
      "ParentDrivingBoneName": "Root",
      "SlotID": 7,
      "Radius": 12.5,
      "LimitType": "ESphericalLimitType::Inner",
      "SourceType": "ECollisionSourceType::DataAsset"
    }
  ],
  "LimitsDataAsset": {
    "ObjectName": "KawaiiPhysicsLimitsDataAsset'DA_Limits'",
    "ObjectPath": "Marvel/Content/Whatever/DA_Limits.0"
  },
  "ComponentPose": { "LinkID": 5 }
}
)JSON");

	TSharedPtr<FJsonObject> NodeObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(NodeJson);
	if (!TestTrue(TEXT("fixture parses as JSON"), FJsonSerializer::Deserialize(Reader, NodeObject) && NodeObject.IsValid()))
	{
		return false;
	}

	FAnimNode_KawaiiPhysics Node;
	TArray<FString> Unresolved;
	if (!TestTrue(TEXT("node converts"), UKawaiiPhysicsJsonImporter::ParseKawaiiNode(NodeObject, Node, Unresolved)))
	{
		return false;
	}

	// --- structure ---
	if (!TestEqual(TEXT("chain count"), Node.Chains.Num(), 1))
	{
		return false;
	}

	const FKawaiiPhysicsChain& Chain = Node.Chains[0];

	// --- plain values ---
	TestEqual(TEXT("root bone"), Chain.BoneSettings.RootBone.BoneName, FName("Hair_001_D1"));
	TestTrue(TEXT("bShouldFixTailBone"), Chain.BoneSettings.bShouldFixTailBone);
	TestEqual(TEXT("dummy bone length"), Chain.BoneSettings.DummyBoneLength, 4.0f);
	TestEqual(TEXT("damping"), Chain.PhysicsSettings.PhysicsSettings.Damping, 0.35f);
	TestEqual(TEXT("limit angle"), Chain.PhysicsSettings.PhysicsSettings.LimitAngle, 5.0f);
	TestEqual(TEXT("teleport distance"), Chain.PhysicsSettings.TeleportDistanceThreshold, 120.0f);
	TestEqual(TEXT("wind scale"), Chain.ExternalForceSettings.WindScale, 2.5f);
	TestTrue(TEXT("wind enabled"), Chain.ExternalForceSettings.bEnableWind);
	TestEqual(TEXT("gravity Z"), Chain.ExternalForceSettings.Gravity.Z, -980.0);
	TestEqual(TEXT("LOD threshold"), Chain.AutoConfiguredLODThreshold, 2);
	TestEqual(TEXT("target framerate"), Node.TargetFramerate, 30);
	TestTrue(TEXT("override framerate"), Node.OverrideTargetFramerate);
	TestTrue(TEXT("world collision"), Node.bAllowWorldCollision);

	// --- enum qualifiers stripped ("EBoneForwardAxis::Y_Negative" -> Y_Negative) ---
	TestEqual(TEXT("bone forward axis"), Chain.BoneSettings.BoneForwardAxis, EBoneForwardAxis::Y_Negative);
	TestEqual(TEXT("planar constraint"), Chain.PhysicsSettings.PlanarConstraint, EPlanarConstraint::Z);
	TestEqual(TEXT("compliance type"), Chain.BoneConstraintSettings.BoneConstraintGlobalComplianceType,
	          EXPBDComplianceType::Rubber);

	// --- fork-only limit fields survive the round trip ---
	if (TestEqual(TEXT("spherical limit count"), Node.SphericalLimits.Num(), 1))
	{
		const FSphericalLimit& Limit = Node.SphericalLimits[0];
		TestTrue(TEXT("bUseParentCompBone"), Limit.bUseParentCompBone);
		TestEqual(TEXT("driving bone"), Limit.DrivingBone.BoneName, FName("Spine_02"));
		TestEqual(TEXT("parent driving bone"), Limit.ParentDrivingBoneName, FName("Root"));
		TestEqual(TEXT("slot id"), Limit.SlotID, 7);
		TestEqual(TEXT("radius"), Limit.Radius, 12.5f);
		TestEqual(TEXT("limit type"), Limit.LimitType, ESphericalLimitType::Inner);
		TestEqual(TEXT("source type"), Limit.SourceType, ECollisionSourceType::DataAsset);
	}

	// --- external force presets ---
	// FModel writes FInstancedStruct as a flat object with no type tag, and
	// InstancedStruct itself has no reflected properties, so this is the part
	// most likely to be silently dropped.
	TestEqual(TEXT("external force preset imported"), Chain.ExternalForceSettings.ExternalForces.Num(), 1);
	if (Chain.ExternalForceSettings.ExternalForces.Num() == 1)
	{
		const FInstancedStruct& Preset = Chain.ExternalForceSettings.ExternalForces[0];
		TestTrue(TEXT("preset has a resolved struct type"), Preset.IsValid());
		if (const FKawaiiPhysics_ExternalForce_Basic* Basic =
			Preset.GetPtr<FKawaiiPhysics_ExternalForce_Basic>())
		{
			TestEqual(TEXT("force direction"), Basic->ForceDir.Z, -1.0);
		}
		else
		{
			AddError(TEXT("preset did not resolve to FKawaiiPhysics_ExternalForce_Basic"));
		}
	}

	// --- runtime state must not be imported ---
	TestEqual(TEXT("ModifyBones is not imported"), Chain.ModifyBones.Num(), 0);

	// --- unresolvable object references are dropped and reported, not silently lost ---
	TestNull(TEXT("limits data asset left empty"), Node.LimitsDataAsset.Get());
	TestNull(TEXT("constraints data asset left empty"),
	         Chain.BoneConstraintSettings.BoneConstraintsDataAsset.Get());
	TestEqual(TEXT("both unresolved references reported"), Unresolved.Num(), 2);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
