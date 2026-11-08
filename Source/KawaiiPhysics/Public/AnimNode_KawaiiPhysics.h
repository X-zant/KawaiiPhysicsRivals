// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License
//
// ---------------------------------------------------------------------------
// SCHEMA NOTE -- read before touching any UPROPERTY in this file.
//
// This header is pinned to the reflected schema of the KawaiiPhysics module in
// the shipped game (build 5.3.2-3761137+++depot_marvel+S9.5_release-Marvel).
// The game cooks with unversioned property serialization, which identifies a
// property purely by its index in the owning struct's schema. That index is the
// property's declaration order in C++.
//
// So: the ORDER of UPROPERTY declarations below is load-bearing. Inserting,
// removing or reordering one silently shifts every property after it and
// corrupts every asset that round-trips through this plugin.
//
// Each property carries its schema index as a comment, e.g. [12]. Those indices
// were read out of the game's own .usmap (see Docs/game-kawaii-schema.txt) and
// cross-checked against the Dumper-7 SDK's memory offsets.
//
// Non-UPROPERTY members are free -- they hold no schema index and may be added
// anywhere. WITH_EDITORONLY_DATA properties must only ever be declared AFTER all
// runtime properties of their struct, because they vanish on cook and would
// otherwise shift the runtime indices between editor and game.
// ---------------------------------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "BoneContainer.h"
#include "BonePose.h"
#include "GameplayTagContainer.h"
#include "Engine/EngineBaseTypes.h"

#include "BoneControllers/AnimNode_AnimDynamics.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"

#if	ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 5
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "AnimNode_KawaiiPhysics.generated.h"

class UKawaiiPhysics_CustomExternalForce;
class UKawaiiPhysicsLimitsDataAsset;
class UKawaiiPhysicsBoneConstraintsDataAsset;

#if ENABLE_ANIM_DEBUG
extern KAWAIIPHYSICS_API TAutoConsoleVariable<bool> CVarAnimNodeKawaiiPhysicsEnable;
extern KAWAIIPHYSICS_API TAutoConsoleVariable<bool> CVarAnimNodeKawaiiPhysicsDebug;
extern KAWAIIPHYSICS_API TAutoConsoleVariable<bool> CVarAnimNodeKawaiiPhysicsDebugLengthRate;
#endif


///////////////////////////////////////////////////////////////////////////////
// Enums
///////////////////////////////////////////////////////////////////////////////

/** Axis to pin each simulated bone to, if any. */
UENUM()
enum class EPlanarConstraint : uint8
{
	None,
	X,
	Y,
	Z,
};

/** Which local axis of a bone points "down the chain". */
UENUM()
enum class EBoneForwardAxis : uint8
{
	X_Positive,
	X_Negative,
	Y_Positive,
	Y_Negative,
	Z_Positive,
	Z_Negative,
};

/**
 * Shape of a collision limit.
 *
 * CapsuleBone is an addition in the game's fork: a capsule whose length and
 * orientation are taken from the driving bone to its child rather than authored.
 */
UENUM()
enum class ECollisionLimitType : uint8
{
	None,
	Spherical,
	Capsule,
	Box,
	Planar,
	CapsuleBone,
};

/** Where a collision limit was authored. */
UENUM()
enum class ECollisionSourceType : uint8
{
	/** Authored directly on the AnimNode */
	AnimNode,
	/** Sourced from a UKawaiiPhysicsLimitsDataAsset */
	DataAsset,
	/** Sourced from a UPhysicsAsset */
	PhysicsAsset,
};

/**
 * Preset compliance values for the XPBD bone constraints.
 * http://blog.mmacklin.com/2016/10/12/xpbd-slides-and-stiffness/
 */
UENUM()
enum class EXPBDComplianceType : uint8
{
	Concrete UMETA(DisplayName = "Concrete"),
	Wood UMETA(DisplayName = "Wood"),
	Leather UMETA(DisplayName = "Leather"),
	Tendon UMETA(DisplayName = "Tendon"),
	Rubber UMETA(DisplayName = "Rubber"),
	Muscle UMETA(DisplayName = "Muscle"),
	Fat UMETA(DisplayName = "Fat"),
};

/**
 * Shape used for a per-bone custom collider.
 *
 * NOTE: "Capusle" is misspelled in the game's enum. It is reproduced verbatim
 * because the enumerator name is part of the serialized schema.
 */
UENUM()
enum class EBoneShapeType : uint8
{
	Sphere,
	Capusle,
};


///////////////////////////////////////////////////////////////////////////////
// Collision limits
///////////////////////////////////////////////////////////////////////////////

/**
 * Base for every authored collision limit.
 *
 * The game's fork adds three fields over upstream: bUseParentCompBone /
 * ParentDrivingBoneName (drive the limit from a bone on the parent component
 * rather than this one, used for attached cosmetics) and SlotID (groups limits
 * so they can be swapped per outfit).
 */
USTRUCT()
struct FCollisionLimitBase
{
	GENERATED_BODY()

	/** [0] Resolve DrivingBone against the parent component's skeleton instead of this one. */
	UPROPERTY(EditAnywhere, Category = CollisionLimitBase)
	bool bUseParentCompBone = false;

	/** [1] Bone this limit is attached to. */
	UPROPERTY(EditAnywhere, Category = CollisionLimitBase, meta = (EditCondition = "!bUseParentCompBone"))
	FBoneReference DrivingBone;

	/** [2] Bone name on the parent component, used when bUseParentCompBone is set. */
	UPROPERTY(EditAnywhere, Category = CollisionLimitBase, meta = (EditCondition = "bUseParentCompBone"))
	FName ParentDrivingBoneName = NAME_None;

	/** [3] Grouping id, so sets of limits can be enabled/disabled together. */
	UPROPERTY(EditAnywhere, Category = CollisionLimitBase)
	int32 SlotID = 0;

	/** [4] Offset from the driving bone. */
	UPROPERTY(EditAnywhere, Category = CollisionLimitBase)
	FVector OffsetLocation = FVector::ZeroVector;

	/** [5] Rotation offset from the driving bone. */
	UPROPERTY(EditAnywhere, Category = CollisionLimitBase, meta = (ClampMin = "-360", ClampMax = "360"))
	FRotator OffsetRotation = FRotator::ZeroRotator;

	/** [6] Resolved component-space location (runtime). */
	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	/** [7] Resolved component-space rotation (runtime). */
	UPROPERTY()
	FQuat Rotation = FQuat::Identity;

	/** [8] Whether this limit currently participates in collision. */
	UPROPERTY()
	bool bEnable = true;

	/** [9] Where this limit came from. */
	UPROPERTY(VisibleAnywhere, Category = CollisionLimitBase)
	ECollisionSourceType SourceType = ECollisionSourceType::AnimNode;

	// --- runtime-only scratch; no schema index ---

	/** The driving bone's component-space scale X this frame (game: limit +0x04). The main node's sphere, capsule
	 *  and box collisions scale the limit by it; the Merge node's do not. */
	float DrivingBoneScale = 1.0f;

	/** For bUseParentCompBone: ParentDrivingBoneName's transform on the parent skeletal mesh, expressed in this
	 *  component's space, cached on the game thread (PreUpdate). */
	FTransform ParentBonePlacement = FTransform::Identity;
	bool bParentBonePlacementValid = false;

	// --- editor-only below this line; must stay last, see SCHEMA NOTE ---
#if WITH_EDITORONLY_DATA

	/** Stable identity for the editor gizmo and for matching against data assets. */
	UPROPERTY(VisibleAnywhere, Category = Debug, meta = (IgnoreForMemberInitializationTest))
	FGuid Guid = FGuid::NewGuid();

	/** Shape discriminator, set by each derived type's constructor. */
	UPROPERTY()
	ECollisionLimitType Type = ECollisionLimitType::None;

#endif

	FCollisionLimitBase& operator=(const FCollisionLimitBase& Other)
	{
		bUseParentCompBone = Other.bUseParentCompBone;
		DrivingBone = Other.DrivingBone;
		ParentDrivingBoneName = Other.ParentDrivingBoneName;
		SlotID = Other.SlotID;
		OffsetLocation = Other.OffsetLocation;
		OffsetRotation = Other.OffsetRotation;
		Location = Other.Location;
		Rotation = Other.Rotation;
		bEnable = Other.bEnable;
		SourceType = Other.SourceType;
		DrivingBoneScale = Other.DrivingBoneScale;
		ParentBonePlacement = Other.ParentBonePlacement;
		bParentBonePlacementValid = Other.bParentBonePlacementValid;
#if WITH_EDITORONLY_DATA
		Guid = Other.Guid;
		Type = Other.Type;
#endif
		return *this;
	}
};

USTRUCT(BlueprintType)
struct FSphericalLimit : public FCollisionLimitBase
{
	GENERATED_BODY()

	FSphericalLimit()
	{
#if WITH_EDITORONLY_DATA
		Type = ECollisionLimitType::Spherical;
#endif
	}

	/** [0] */
	UPROPERTY(EditAnywhere, Category = SphericalLimit, meta = (ClampMin = "0"))
	float Radius = 5.0f;

	/** [1] Push bones out of the sphere (Outer) or keep them inside it (Inner). */
	UPROPERTY(EditAnywhere, Category = SphericalLimit)
	ESphericalLimitType LimitType = ESphericalLimitType::Outer;

	FSphericalLimit& operator=(const FSphericalLimit& Other)
	{
		FCollisionLimitBase::operator=(Other);
		Radius = Other.Radius;
		LimitType = Other.LimitType;
		return *this;
	}
};

USTRUCT(BlueprintType)
struct FCapsuleLimit : public FCollisionLimitBase
{
	GENERATED_BODY()

	FCapsuleLimit()
	{
#if WITH_EDITORONLY_DATA
		Type = ECollisionLimitType::Capsule;
#endif
	}

	/** [0] */
	UPROPERTY(EditAnywhere, Category = CapsuleLimit, meta = (ClampMin = "0"))
	float Radius = 5.0f;

	/** [1] Length of the cylindrical section, excluding the two caps. */
	UPROPERTY(EditAnywhere, Category = CapsuleLimit, meta = (ClampMin = "0"))
	float Length = 10.0f;

	FCapsuleLimit& operator=(const FCapsuleLimit& Other)
	{
		FCollisionLimitBase::operator=(Other);
		Radius = Other.Radius;
		Length = Other.Length;
		return *this;
	}
};

USTRUCT(BlueprintType)
struct FBoxLimit : public FCollisionLimitBase
{
	GENERATED_BODY()

	FBoxLimit()
	{
#if WITH_EDITORONLY_DATA
		Type = ECollisionLimitType::Box;
#endif
	}

	/** [0] */
	UPROPERTY(EditAnywhere, Category = BoxLimit)
	FVector Extent = FVector(5.0f, 5.0f, 5.0f);

	FBoxLimit& operator=(const FBoxLimit& Other)
	{
		FCollisionLimitBase::operator=(Other);
		Extent = Other.Extent;
		return *this;
	}
};

USTRUCT(BlueprintType)
struct FPlanarLimit : public FCollisionLimitBase
{
	GENERATED_BODY()

	FPlanarLimit()
	{
#if WITH_EDITORONLY_DATA
		Type = ECollisionLimitType::Planar;
#endif
	}

	/** [0] */
	UPROPERTY()
	FPlane Plane = FPlane(0, 0, 0, 0);

	FPlanarLimit& operator=(const FPlanarLimit& Other)
	{
		FCollisionLimitBase::operator=(Other);
		Plane = Other.Plane;
		return *this;
	}
};


///////////////////////////////////////////////////////////////////////////////
// Per-chain building blocks
///////////////////////////////////////////////////////////////////////////////

/** An extra root added to a chain, with its own optional exclude list. */
USTRUCT(BlueprintType)
struct KAWAIIPHYSICS_API FKawaiiPhysicsRootBoneSetting
{
	GENERATED_BODY()

	/** [0] Simulate this bone and everything below it. */
	UPROPERTY(EditAnywhere, Category = "Bones")
	FBoneReference RootBone;

	/** [1] Exclude list that replaces the chain's own, when enabled. */
	UPROPERTY(EditAnywhere, Category = "Bones", meta = (EditCondition = "bUseOverrideExcludeBones"))
	TArray<FBoneReference> OverrideExcludeBones;

	/** [2] */
	UPROPERTY(EditAnywhere, Category = "Bones", meta = (InlineEditConditionToggle))
	bool bUseOverrideExcludeBones = false;
};

/**
 * The per-bone physics parameters actually consumed by the solver.
 *
 * LimitLinear and GravityScale are additions in the game's fork.
 */
USTRUCT(BlueprintType)
struct KAWAIIPHYSICS_API FKawaiiPhysicsSettings
{
	GENERATED_BODY()

	/** [0] Lower damps less, so acceleration shows through more. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"), Category = "KawaiiPhysics")
	float Damping = 0.1f;

	/** [1] Higher keeps the bone closer to its animated pose. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"), Category = "KawaiiPhysics")
	float Stiffness = 0.05f;

	/** [2] How much world-space translation of the component feeds the sim. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"), Category = "KawaiiPhysics")
	float WorldDampingLocation = 0.8f;

	/** [3] How much world-space rotation of the component feeds the sim. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"), Category = "KawaiiPhysics")
	float WorldDampingRotation = 0.8f;

	/** [4] Collision radius of the bone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0", DisplayName = "Collision Radius"),
		Category = "KawaiiPhysics")
	float Radius = 3.0f;

	/** [5] Max angular deviation from the pose direction, in degrees. 0 disables. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"), Category = "KawaiiPhysics")
	float LimitAngle = 0.0f;

	/** [6] Max linear deviation from the pose position. 0 disables. (fork addition) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"), Category = "KawaiiPhysics")
	float LimitLinear = 0.0f;

	/** [7] Scales the chain's gravity for this bone. (fork addition) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KawaiiPhysics")
	float GravityScale = 1.0f;
};

/**
 * One simulated bone.
 *
 * Fork additions over upstream: Rotation / OffsetRotation (the solver tracks an
 * explicit orientation rather than deriving it purely from the child direction),
 * bFixed, bHasFollowBone, and the two wave-animation fields.
 */
USTRUCT(BlueprintType)
struct KAWAIIPHYSICS_API FKawaiiPhysicsModifyBone
{
	GENERATED_USTRUCT_BODY()

	/** [0] */
	UPROPERTY()
	FBoneReference BoneRef;

	/** [1] Index into the owning chain's ModifyBones. */
	UPROPERTY(BlueprintReadOnly, Category = "KawaiiPhysics|ModifyBone")
	int32 Index = -1;

	/** [2] */
	UPROPERTY(BlueprintReadOnly, Category = "KawaiiPhysics|ModifyBone")
	int32 ParentIndex = -1;

	/** [3] */
	UPROPERTY(BlueprintReadOnly, Category = "KawaiiPhysics|ModifyBone")
	TArray<int32> ChildIndices;

	/** [4] Resolved settings for this bone, after curves and per-bone overrides. */
	UPROPERTY(BlueprintReadOnly, Category = "KawaiiPhysics|ModifyBone")
	FKawaiiPhysicsSettings PhysicsSettings;

	/** [5] Current simulated component-space location. */
	UPROPERTY(BlueprintReadWrite, Category = "KawaiiPhysics|ModifyBone")
	FVector Location = FVector::ZeroVector;

	/** [6] Current simulated component-space rotation. (fork addition) */
	UPROPERTY(BlueprintReadWrite, Category = "KawaiiPhysics|ModifyBone")
	FQuat Rotation = FQuat::Identity;

	/** [7] Extra rotation layered on top of the solved orientation. (fork addition) */
	UPROPERTY(BlueprintReadWrite, Category = "KawaiiPhysics|ModifyBone")
	FQuat OffsetRotation = FQuat::Identity;

	/** [8] */
	UPROPERTY(BlueprintReadOnly, Category = "KawaiiPhysics|ModifyBone")
	FVector PrevLocation = FVector::ZeroVector;

	/** [9] */
	UPROPERTY(BlueprintReadOnly, Category = "KawaiiPhysics|ModifyBone")
	FQuat PrevRotation = FQuat::Identity;

	/** [10] Component-space location straight from the incoming pose. */
	UPROPERTY(BlueprintReadOnly, Category = "KawaiiPhysics|ModifyBone")
	FVector PoseLocation = FVector::ZeroVector;

	/** [11] */
	UPROPERTY(BlueprintReadOnly, Category = "KawaiiPhysics|ModifyBone")
	FQuat PoseRotation = FQuat::Identity;

	/** [12] */
	UPROPERTY(BlueprintReadOnly, Category = "KawaiiPhysics|ModifyBone")
	FVector PoseScale = FVector::OneVector;

	/** [13] Distance from the chain root, along the bone chain. */
	UPROPERTY(BlueprintReadOnly, Category = "KawaiiPhysics|ModifyBone")
	float LengthFromRoot = 0.0f;

	/** [14] LengthFromRoot normalised to 0..1 over the chain. Drives every curve. */
	UPROPERTY(BlueprintReadOnly, Category = "KawaiiPhysics|ModifyBone")
	float LengthRateFromRoot = 0.0f;

	/** [15] Synthetic tail bone appended past the real end bone. */
	UPROPERTY(BlueprintReadOnly, Category = "KawaiiPhysics|ModifyBone")
	bool bDummy = false;

	/** [16] Excluded from simulation but still part of the hierarchy. */
	UPROPERTY(BlueprintReadOnly, Category = "KawaiiPhysics|ModifyBone")
	bool bSkipSimulate = false;

	/** [17] Pinned to the pose: never integrated. (fork addition) */
	UPROPERTY()
	bool bFixed = false;

	/** [18] This bone is driven by FBoneSettings::FollowBone. (fork addition) */
	UPROPERTY()
	bool bHasFollowBone = false;

	/** [19] At or below FWaveAnimSettings::WaveBeginBone. (fork addition) */
	UPROPERTY()
	bool bChildOfWaveBeginBone = false;

	/** [20] Wave amplitude resolved for this bone from the wave curve. (fork addition) */
	UPROPERTY()
	float WaveAmplitude = 0.0f;

	// --- runtime-only scratch; no schema index, safe to extend ---

	/** Set when this bone's transform could not be resolved this frame. */
	bool bBoneMissing = false;

	/** Set by the relative-rotation limit this pass (game: bone +0x152); the parent's output rotation is then
	 *  LimitedParentRotation (game: bone +0x160) instead of the usual derivation. */
	bool bRotationLimited = false;
	FQuat LimitedParentRotation = FQuat::Identity;

	/** From the first capsule CustomShape driven by this bone (game: bone +0x44 = HalfHeight, and OffsetRotation).
	 *  A positive half height makes capsule-limit collision treat the bone as a segment along the Z axis of
	 *  ShapeRotation (= PoseRotation * OffsetRotation; game: bone +0x70). */
	float ShapeHalfHeight = 0.0f;
	FQuat ShapeRotation = FQuat::Identity;

	/** The output keeps this bone's pose location this frame (its direction from the parent did not change). */
	bool bKeepPoseLocation = false;

	void UpdatePoseTransform(const FBoneContainer& BoneContainer, FCSPose<FCompactPose>& Pose,
	                         bool ResetBoneTransformWhenBoneNotFound)
	{
		const auto CompactPoseIndex = BoneRef.GetCompactPoseIndex(BoneContainer);
		if (CompactPoseIndex < 0)
		{
			bBoneMissing = true;
			// Resetting here breaks LOD transitions, so it is opt-in. See upstream #44.
			if (ResetBoneTransformWhenBoneNotFound)
			{
				PoseLocation = FVector::ZeroVector;
				PoseRotation = FQuat::Identity;
				PoseScale = FVector::OneVector;
			}
			return;
		}

		bBoneMissing = false;
		const auto ComponentSpaceTransform = Pose.GetComponentSpaceTransform(CompactPoseIndex);
		PoseLocation = ComponentSpaceTransform.GetLocation();
		PoseRotation = ComponentSpaceTransform.GetRotation();
		PoseScale = ComponentSpaceTransform.GetScale3D();
	}

	bool HasParent() const { return ParentIndex >= 0; }

	/** A bone is integrated only if it is neither pinned nor explicitly skipped. */
	bool IsSimulated() const { return !bFixed && !bSkipSimulate; }

	FKawaiiPhysicsModifyBone()
	{
	}
};

/** A distance constraint between two simulated bones, solved with XPBD. */
USTRUCT()
struct FModifyBoneConstraint
{
	GENERATED_BODY()

	FModifyBoneConstraint()
	{
	}

	/** [0] */
	UPROPERTY(EditAnywhere, Category = "KawaiiPhysics")
	FBoneReference Bone1;

	/** [1] */
	UPROPERTY(EditAnywhere, Category = "KawaiiPhysics")
	FBoneReference Bone2;

	/** [2] */
	UPROPERTY(EditAnywhere, Category = "KawaiiPhysics", meta = (InlineEditConditionToggle))
	bool bOverrideCompliance = false;

	/** [3] */
	UPROPERTY(EditAnywhere, Category = "KawaiiPhysics", meta = (EditCondition = "bOverrideCompliance"))
	EXPBDComplianceType ComplianceType = EXPBDComplianceType::Leather;

	/** [4] Resolved index into the owning chain's ModifyBones. */
	UPROPERTY()
	int32 ModifyBoneIndex1 = -1;

	/** [5] */
	UPROPERTY()
	int32 ModifyBoneIndex2 = -1;

	/** [6] Rest length, captured from the reference pose. */
	UPROPERTY()
	float Length = -1.0f;

	/** [7] Auto-generated to hold a dummy tail bone in place. */
	UPROPERTY()
	bool bIsDummy = false;

	/** [8] XPBD Lagrange multiplier, carried across iterations within a frame. */
	UPROPERTY()
	float Lambda = 0.0f;

	FORCEINLINE bool operator ==(const FModifyBoneConstraint& Other) const
	{
		return ((Bone1 == Other.Bone1 && Bone2 == Other.Bone2) || (Bone1 == Other.Bone2 && Bone2 == Other.Bone1)) &&
			ComplianceType == Other.ComplianceType;
	}

	void InitializeBone(const FBoneContainer& RequiredBones)
	{
		Bone1.Initialize(RequiredBones);
		Bone2.Initialize(RequiredBones);
	}

	bool IsBoneReferenceValid() const { return ModifyBoneIndex1 >= 0 && ModifyBoneIndex2 >= 0; }

	bool IsValid() const { return Length > 0.0f; }
};

/**
 * A per-bone collider carried by the simulated bone itself, as opposed to the
 * authored limits which the bones collide against. Fork addition.
 */
USTRUCT(BlueprintType)
struct FBoneShape
{
	GENERATED_BODY()

	/** [0] */
	UPROPERTY(EditAnywhere, Category = "BoneShape")
	FBoneReference DrivingBone;

	/** [1] */
	UPROPERTY(EditAnywhere, Category = "BoneShape")
	EBoneShapeType ShapeType = EBoneShapeType::Sphere;

	/** [2] Half length of the capsule section; unused for spheres. */
	UPROPERTY(EditAnywhere, Category = "BoneShape", meta = (ClampMin = "0"))
	float HalfHeight = 0.0f;

	/** [3] */
	UPROPERTY(EditAnywhere, Category = "BoneShape", meta = (ClampMin = "-360", ClampMax = "360"))
	FRotator OffsetRotation = FRotator::ZeroRotator;
};

/**
 * Per-bone overrides, indexed positionally against a chain's ModifyBones.
 *
 * Each value has its own bNeedEdit toggle; when the toggle is off the value
 * from the chain's shared FKawaiiPhysicsSettings (times its curve) is used.
 * SpringStiffness and the relative-rotation limits are fork additions.
 */
USTRUCT(BlueprintType)
struct FBonePhysicsParams
{
	GENERATED_BODY()

	/** [0] */
	UPROPERTY(EditAnywhere, Category = "BonePhysicsParams", meta = (EditCondition = "bNeedEditDamping"))
	float Damping = 0.1f;
	/** [1] */
	UPROPERTY(EditAnywhere, Category = "BonePhysicsParams", meta = (InlineEditConditionToggle))
	bool bNeedEditDamping = false;

	/** [2] */
	UPROPERTY(EditAnywhere, Category = "BonePhysicsParams", meta = (EditCondition = "bNeedEditStiffness"))
	float Stiffness = 0.05f;
	/** [3] */
	UPROPERTY(EditAnywhere, Category = "BonePhysicsParams", meta = (InlineEditConditionToggle))
	bool bNeedEditStiffness = false;

	/** [4] Extra spring pulling the bone back to its pose position. No toggle. */
	UPROPERTY(EditAnywhere, Category = "BonePhysicsParams", meta = (ClampMin = "0"))
	float SpringStiffness = 0.0f;

	/** [5] */
	UPROPERTY(EditAnywhere, Category = "BonePhysicsParams", meta = (EditCondition = "bNeedEditWorldDampingLocation"))
	float WorldDampingLocation = 0.8f;
	/** [6] */
	UPROPERTY(EditAnywhere, Category = "BonePhysicsParams", meta = (InlineEditConditionToggle))
	bool bNeedEditWorldDampingLocation = false;

	/** [7] */
	UPROPERTY(EditAnywhere, Category = "BonePhysicsParams", meta = (EditCondition = "bNeedEditWorldDampingRotation"))
	float WorldDampingRotation = 0.8f;
	/** [8] */
	UPROPERTY(EditAnywhere, Category = "BonePhysicsParams", meta = (InlineEditConditionToggle))
	bool bNeedEditWorldDampingRotation = false;

	/** [9] */
	UPROPERTY(EditAnywhere, Category = "BonePhysicsParams", meta = (EditCondition = "bNeedEditRadius"))
	float Radius = 3.0f;
	/** [10] */
	UPROPERTY(EditAnywhere, Category = "BonePhysicsParams", meta = (InlineEditConditionToggle))
	bool bNeedEditRadius = false;

	/** [11] */
	UPROPERTY(EditAnywhere, Category = "BonePhysicsParams", meta = (EditCondition = "bNeedEditLimitAngle"))
	float LimitAngle = 0.0f;
	/** [12] */
	UPROPERTY(EditAnywhere, Category = "BonePhysicsParams", meta = (InlineEditConditionToggle))
	bool bNeedEditLimitAngle = false;

	/** [13] */
	UPROPERTY(EditAnywhere, Category = "BonePhysicsParams", meta = (EditCondition = "bNeedEditLimitLinear"))
	float LimitLinear = 0.0f;
	/** [14] */
	UPROPERTY(EditAnywhere, Category = "BonePhysicsParams", meta = (InlineEditConditionToggle))
	bool bNeedEditLimitLinear = false;

	/** [15] */
	UPROPERTY(EditAnywhere, Category = "BonePhysicsParams", meta = (EditCondition = "bNeedEditGravityScale"))
	float GravityScale = 1.0f;
	/** [16] */
	UPROPERTY(EditAnywhere, Category = "BonePhysicsParams", meta = (InlineEditConditionToggle))
	bool bNeedEditGravityScale = false;

	/** [17] Clamp the bone's rotation relative to its parent to the allowed half-axes below. */
	UPROPERTY(EditAnywhere, Category = "BonePhysicsParams")
	bool bLimitRelativeRotation = false;

	/** [18] */
	UPROPERTY(EditAnywhere, Category = "BonePhysicsParams", meta = (EditCondition = "bLimitRelativeRotation"))
	bool bAllowPositiveRelativeRotationX = true;
	/** [19] */
	UPROPERTY(EditAnywhere, Category = "BonePhysicsParams", meta = (EditCondition = "bLimitRelativeRotation"))
	bool bAllowNegativeRelativeRotationX = true;
	/** [20] */
	UPROPERTY(EditAnywhere, Category = "BonePhysicsParams", meta = (EditCondition = "bLimitRelativeRotation"))
	bool bAllowPositiveRelativeRotationY = true;
	/** [21] */
	UPROPERTY(EditAnywhere, Category = "BonePhysicsParams", meta = (EditCondition = "bLimitRelativeRotation"))
	bool bAllowNegativeRelativeRotationY = true;
	/** [22] */
	UPROPERTY(EditAnywhere, Category = "BonePhysicsParams", meta = (EditCondition = "bLimitRelativeRotation"))
	bool bAllowPositiveRelativeRotationZ = true;
	/** [23] */
	UPROPERTY(EditAnywhere, Category = "BonePhysicsParams", meta = (EditCondition = "bLimitRelativeRotation"))
	bool bAllowNegativeRelativeRotationZ = true;
};

/** Which bones a chain covers, and how the chain is terminated. */
USTRUCT(BlueprintType)
struct FBoneSettings
{
	GENERATED_BODY()

	/** [0] Simulate this bone and everything below it. */
	UPROPERTY(EditAnywhere, Category = "Bones")
	FBoneReference RootBone;

	/** [1] Drop these bones and their descendants from the chain. */
	UPROPERTY(EditAnywhere, Category = "Bones")
	TArray<FBoneReference> ExcludeBones;

	/** [2] Whether the root bone itself is integrated, or pinned to the pose. */
	UPROPERTY(EditAnywhere, Category = "Bones")
	bool bRootBoneSimulate = false;

	/** [3] Pin the last real bone of the chain to its pose. */
	UPROPERTY(EditAnywhere, Category = "Bones")
	bool bShouldFixTailBone = false;

	/** [4] An additional bone pinned to its pose, wherever it sits in the chain. */
	UPROPERTY(EditAnywhere, Category = "Bones")
	FBoneReference FixedBone;

	/** [5] Bones in this chain track this bone's motion rather than being free. */
	UPROPERTY(EditAnywhere, Category = "Bones")
	FBoneReference FollowBone;

	/** [6] */
	UPROPERTY(EditAnywhere, Category = "Bones")
	TArray<FKawaiiPhysicsRootBoneSetting> AdditionalRootBones;

	/** [7] Length of the synthetic tail bone; 0 disables it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bones", meta = (ClampMin = "0"))
	float DummyBoneLength = 0.0f;

	/** [8] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bones")
	EBoneForwardAxis BoneForwardAxis = EBoneForwardAxis::X_Positive;

	// --- runtime-only scratch ---

	/** Resolved once per CacheBones: indices of ExcludeBones that actually resolved. */
	TArray<FCompactPoseBoneIndex> ResolvedExcludeBoneIndices;
};

/**
 * Sine-wave displacement layered on top of the simulation. Fork addition.
 *
 * Drives a travelling wave down the chain, used for capes and long hair so they
 * keep moving when the character is standing still.
 */
USTRUCT(BlueprintType)
struct FWaveAnimSettings
{
	GENERATED_BODY()

	/** [0] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WaveAnim")
	bool bEnableWaveAnim = false;

	/** [1] Wave starts here; bones above it are unaffected. */
	UPROPERTY(EditAnywhere, Category = "WaveAnim")
	FBoneReference WaveBeginBone;

	/** [2] Oscillations per second. (spelling matches the game's schema) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WaveAnim")
	float WaveFrequncy = 1.0f;

	/** [3] Number of full waves along the affected span. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WaveAnim")
	float WaveNum = 1.0f;

	/** [4] Displacement direction, in component space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WaveAnim")
	FVector WaveDirection = FVector::ZeroVector;

	/** [5] Peak displacement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WaveAnim")
	float WaveAmplitude = 0.0f;

	/** [6] Scales WaveAmplitude by position along the affected span (0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WaveAnim",
		meta = (DisplayName = "Wave Amplitude by Bone Length Rate"))
	FRuntimeFloatCurve WaveAmplitudeCurveData;
};

/** The physics half of a chain: shared settings, per-bone overrides, and curves. */
USTRUCT(BlueprintType)
struct FBoneChainPhysicsSettings
{
	GENERATED_BODY()

	/** [0] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", meta = (DisplayPriority = 0))
	FKawaiiPhysicsSettings PhysicsSettings;

	/** [1] */
	UPROPERTY(EditAnywhere, Category = "Physics Settings")
	TArray<FBoneShape> CustomShapes;

	/** [2] Use PhysicsParamsPerBone instead of the curves below. */
	UPROPERTY(EditAnywhere, Category = "Physics Settings")
	bool bUseCurve = false;

	/** [3] Positional overrides, one entry per simulated bone. */
	UPROPERTY(EditAnywhere, Category = "Physics Settings", meta = (EditCondition = "!bUseCurve"))
	TArray<FBonePhysicsParams> PhysicsParamsPerBone;

	/** [4] Component moves further than this in one frame are not fed to the sim. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings")
	float TeleportDistanceThreshold = 300.0f;

	/** [5] Component rotates more than this in one frame and it is not fed to the sim. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings")
	float TeleportRotationThreshold = 10.0f;

	/** [6] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings")
	EPlanarConstraint PlanarConstraint = EPlanarConstraint::None;

	// Every curve below is sampled at FKawaiiPhysicsModifyBone::LengthRateFromRoot
	// and multiplies the matching field of PhysicsSettings.

	/** [7] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings",
		meta = (DisplayName = "LimitLinear Rate by Bone Length Rate"))
	FRuntimeFloatCurve LimitLinearCurveData;

	/** [8] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings",
		meta = (DisplayName = "Gravity Rate by Bone Length Rate"))
	FRuntimeFloatCurve GravityCurveData;

	/** [9] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", AdvancedDisplay,
		meta = (DisplayName = "Damping Rate by Bone Length Rate"))
	FRuntimeFloatCurve DampingCurveData;

	/** [10] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", AdvancedDisplay,
		meta = (DisplayName = "Stiffness Rate by Bone Length Rate"))
	FRuntimeFloatCurve StiffnessCurveData;

	/** [11] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", AdvancedDisplay,
		meta = (DisplayName = "World Damping Location Rate by Bone Length Rate"))
	FRuntimeFloatCurve WorldDampingLocationCurveData;

	/** [12] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", AdvancedDisplay,
		meta = (DisplayName = "World Damping Rotation Rate by Bone Length Rate"))
	FRuntimeFloatCurve WorldDampingRotationCurveData;

	/** [13] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", AdvancedDisplay,
		meta = (DisplayName = "Radius Rate by Bone Length Rate"))
	FRuntimeFloatCurve RadiusCurveData;

	/** [14] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", AdvancedDisplay,
		meta = (DisplayName = "LimitAngle Rate by Bone Length Rate"))
	FRuntimeFloatCurve LimitAngleCurveData;
};

/** XPBD bone-constraint configuration for one chain. */
USTRUCT(BlueprintType)
struct FBoneConstraintSettings
{
	GENERATED_BODY()

	/** [0] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bone Constraint")
	EXPBDComplianceType BoneConstraintGlobalComplianceType = EXPBDComplianceType::Leather;

	/** [1] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bone Constraint")
	int32 BoneConstraintIterationCountBeforeCollision = 1;

	/** [2] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bone Constraint")
	int32 BoneConstraintIterationCountAfterCollision = 1;

	/** [3] Also constrain the dummy tail bones of any constrained end bones. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bone Constraint")
	bool bAutoAddChildDummyBoneConstraint = true;

	/** [4] */
	UPROPERTY(EditAnywhere, Category = "Bone Constraint", meta = (TitleProperty = "{Bone1} - {Bone2}"))
	TArray<FModifyBoneConstraint> BoneConstraints;

	/** [5] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bone Constraint")
	TObjectPtr<UKawaiiPhysicsBoneConstraintsDataAsset> BoneConstraintsDataAsset = nullptr;

	/** [6] Preview of what BoneConstraintsDataAsset resolves to. */
	UPROPERTY(VisibleAnywhere, Category = "Bone Constraint", AdvancedDisplay,
		meta = (TitleProperty = "{Bone1} - {Bone2}"))
	TArray<FModifyBoneConstraint> BoneConstraintsData;

	// --- runtime-only scratch ---

	/** BoneConstraints + BoneConstraintsData + auto-generated dummy constraints. */
	TArray<FModifyBoneConstraint> MergedBoneConstraints;
};

/** Gravity, wind and the external-force stack for one chain. */
USTRUCT(BlueprintType)
struct FExternalForceSettings
{
	GENERATED_BODY()

	/** [0] Constant acceleration in component space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ExternalForce", meta = (DisplayName = "External Force"))
	FVector Gravity = FVector::ZeroVector;

	/** [1] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ExternalForce", meta = (InlineEditConditionToggle))
	bool bEnableWind = false;

	/** [2] Scale applied to WindDirectionalSource contributions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ExternalForce", meta = (EditCondition = "bEnableWind"))
	float WindScale = 1.0f;

	/** [3] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ExternalForce",
		meta = (BaseStruct = "/Script/KawaiiPhysics.KawaiiPhysics_ExternalForce", ExcludeBaseStruct))
	TArray<FInstancedStruct> ExternalForces;

	/** [4] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = "ExternalForce")
	TArray<TObjectPtr<UKawaiiPhysics_CustomExternalForce>> CustomExternalForces;
};

/**
 * One independently-simulated bone chain.
 *
 * This is the central difference from upstream: upstream carries exactly one
 * chain's worth of settings flat on the node, whereas the game's fork holds an
 * array of these and runs the whole solver per entry.
 */
USTRUCT(BlueprintType)
struct KAWAIIPHYSICS_API FKawaiiPhysicsChain
{
	GENERATED_BODY()

	/** [0] */
	UPROPERTY(EditAnywhere, Category = "Bones")
	FBoneSettings BoneSettings;

	/** [1] */
	UPROPERTY(EditAnywhere, Category = "Physics Settings")
	FBoneChainPhysicsSettings PhysicsSettings;

	/** [2] */
	UPROPERTY(EditAnywhere, Category = "Bone Constraint")
	FBoneConstraintSettings BoneConstraintSettings;

	/** [3] */
	UPROPERTY(EditAnywhere, Category = "ExternalForce")
	FExternalForceSettings ExternalForceSettings;

	/** [4] */
	UPROPERTY(EditAnywhere, Category = "WaveAnim")
	FWaveAnimSettings WaveAnimSettings;

	/** [5] */
	UPROPERTY(BlueprintReadWrite, Category = "Bones")
	TArray<FKawaiiPhysicsModifyBone> ModifyBones;

	/** [6] Longest root-to-tip distance in the chain; normalises LengthRateFromRoot. */
	UPROPERTY(Transient)
	float TotalBoneLength = 0.0f;

	/** [7] LengthFromRoot of WaveBeginBone, so the wave can be offset to start there. */
	UPROPERTY(Transient)
	float WaveBeginBoneLengthFromRoot = 0.0f;

	/** [8] Skip this chain above this LOD (it still runs AT this LOD). -1 means never skip. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
	int32 AutoConfiguredLODThreshold = -1;

	/** [9] Owner's net role, cached so simulated proxies can be cheapened. */
	UPROPERTY(Transient)
	TEnumAsByte<ENetRole> CachedOwnerNetRole = ROLE_None;

	// --- runtime-only scratch; no schema index ---

	/** Component translation since the previous evaluation, in component space (game: chain +0x900). */
	FVector SkelCompMoveVector = FVector::ZeroVector;

	/** Component rotation since the previous evaluation (game: chain +0x920). */
	FQuat SkelCompMoveRotation = FQuat::Identity;

	/** Where the fixed bone should sit, carried by the follow bone (game: chain +0x240): FixedRefRelToFollow times
	 *  the follow bone's component-space pose. Updated only while a fixed bone has a follow bone. */
	FTransform FollowTargetTransform = FTransform::Identity;

	/** The fixed bone's reference pose relative to the follow bone's (game: chain +0x2A0). */
	FTransform FixedRefRelToFollow = FTransform::Identity;

	/** Copy of BoneSettings::FollowBone, resolved for this pose (game: chain +0x220). */
	FBoneReference FollowBoneRef;

	/** The skeleton parent of RootBone (game: chain +0x70), source of the virtual parent's pose. */
	FBoneReference RootParentRef;

	/** The parent a simulated root (bRootBoneSimulate) is solved against (game: chain +0x90). */
	FKawaiiPhysicsModifyBone VirtualParentBone;
	bool bHasVirtualParent = false;

	/** Cleared whenever the chain's bone set has to be rebuilt. */
	bool bInitialized = false;

	/** The game's rule (EvaluateSkeletalControl 0x76749B0, SimulateModifyBones 0x767B9F0): a chain runs when its
	 *  threshold is -1 or >= the current LOD. */
	bool IsEnabledForLOD(int32 InLODLevel) const
	{
		return AutoConfiguredLODThreshold == -1 || AutoConfiguredLODThreshold >= InLODLevel;
	}
};


///////////////////////////////////////////////////////////////////////////////
// The node
///////////////////////////////////////////////////////////////////////////////

USTRUCT(BlueprintType)
struct KAWAIIPHYSICS_API FAnimNode_KawaiiPhysics : public FAnimNode_SkeletalControlBase
{
	GENERATED_USTRUCT_BODY()

	/** [0] Every chain simulated by this node. */
	UPROPERTY(EditAnywhere, Category = "Chains")
	TArray<FKawaiiPhysicsChain> Chains;

	/** [1] Fixed timestep the solver is tuned against. */
	UPROPERTY(EditAnywhere, Category = "Physics Settings", meta = (EditCondition = "OverrideTargetFramerate"))
	int32 TargetFramerate = 60;

	/** [2] */
	UPROPERTY(EditAnywhere, Category = "Physics Settings", meta = (InlineEditConditionToggle))
	bool OverrideTargetFramerate = false;

	/** [3] Frames of simulation to burn before the first visible frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings",
		meta = (PinHiddenByDefault, EditCondition = "bNeedWarmUp", ClampMin = "0"))
	int32 WarmUpFrames = 0;

	/** [4] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings",
		meta = (PinHiddenByDefault, EditCondition = "bNeedWarmUp"))
	bool bUseWarmUpWhenResetDynamics = true;

	/** [5] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings",
		meta = (PinHiddenByDefault, InlineEditConditionToggle))
	bool bNeedWarmUp = false;

	/** [6] Re-resolve per-bone settings every frame. Off is faster but freezes them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", AdvancedDisplay,
		meta = (PinHiddenByDefault))
	bool bUpdatePhysicsSettingsInGame = true;

	/** [7] Generally leave off; resetting breaks LOD transitions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", AdvancedDisplay,
		meta = (PinHiddenByDefault))
	bool ResetBoneTransformWhenBoneNotFound = false;

	/** [8] Limits below are shared by every chain on this node. */
	UPROPERTY(EditAnywhere, Category = "Limits")
	TArray<FSphericalLimit> SphericalLimits;

	/** [9] */
	UPROPERTY(EditAnywhere, Category = "Limits")
	TArray<FCapsuleLimit> CapsuleLimits;

	/** [10] */
	UPROPERTY(EditAnywhere, Category = "Limits")
	TArray<FBoxLimit> BoxLimits;

	/** [11] */
	UPROPERTY(EditAnywhere, Category = "Limits")
	TArray<FPlanarLimit> PlanarLimits;

	/** [12] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limits", meta = (PinHiddenByDefault))
	TObjectPtr<UKawaiiPhysicsLimitsDataAsset> LimitsDataAsset = nullptr;

	/** [13] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limits", meta = (PinHiddenByDefault))
	TObjectPtr<UPhysicsAsset> PhysicsAssetForLimits = nullptr;

	/** [14] Limits resolved from LimitsDataAsset / PhysicsAssetForLimits. */
	UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category = "Limits")
	TArray<FSphericalLimit> SphericalLimitsData;

	/** [15] */
	UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category = "Limits")
	TArray<FCapsuleLimit> CapsuleLimitsData;

	/** [16] */
	UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category = "Limits")
	TArray<FBoxLimit> BoxLimitsData;

	/** [17] */
	UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category = "Limits")
	TArray<FPlanarLimit> PlanarLimitsData;

	/** [18] Trace against world collision. Expensive. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Collision", meta = (PinHiddenByDefault))
	bool bAllowWorldCollision = false;

	/** [19] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Collision",
		meta = (PinHiddenByDefault, InlineEditConditionToggle))
	bool bOverrideCollisionParams = false;

	/** [20] */
	UPROPERTY(EditAnywhere, Category = "World Collision",
		meta = (PinHiddenByDefault, EditCondition = "bOverrideCollisionParams",
			DisplayName = "Override SkelComp Collision Params"))
	FBodyInstance CollisionChannelSettings;

	/** [21] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Collision",
		meta = (PinHiddenByDefault, EditCondition = "bAllowWorldCollision"))
	bool bIgnoreSelfComponent = true;

	/** [22] */
	UPROPERTY(EditAnywhere, Category = "World Collision", meta = (EditCondition = "!bIgnoreSelfComponent"))
	TArray<FBoneReference> IgnoreBones;

	/** [23] */
	UPROPERTY(EditAnywhere, Category = "World Collision", meta = (EditCondition = "!bIgnoreSelfComponent"))
	TArray<FName> IgnoreBoneNamePrefix;

	/** [24] Used to target this node from AnimNotifies and the Blueprint library. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag")
	FGameplayTag KawaiiPhysicsTag;

	/** [25] Delta time of the frame currently being simulated. */
	UPROPERTY(BlueprintReadOnly, Category = "KawaiiPhysics")
	float DeltaTime = 0.0f;

	// =========================================================================
	// [26] .. [74] -- deprecated single-chain layout.
	//
	// These are the pre-Chains properties. They still occupy schema indices and
	// are still loaded, so that assets authored before the refactor can be
	// upgraded; CPF_Deprecated means they are never written back out.
	//
	// The "_DEPRECATED" suffix is stripped by UHT: each of these reflects under
	// its bare name (RootBone, ExcludeBones, ...) with CPF_Deprecated set, which
	// is exactly what the game's schema shows. Do not rename them.
	//
	// UpgradeDeprecatedProperties() folds these into Chains[0] on load.
	// =========================================================================

	/** [26] */
	UPROPERTY()
	FBoneReference RootBone_DEPRECATED;
	/** [27] */
	UPROPERTY()
	TArray<FBoneReference> ExcludeBones_DEPRECATED;
	/** [28] */
	UPROPERTY()
	bool bRootBoneSimulate_DEPRECATED = false;
	/** [29] */
	UPROPERTY()
	bool bShouldFixTailBone_DEPRECATED = false;
	/** [30] */
	UPROPERTY()
	FBoneReference FixedBone_DEPRECATED;
	/** [31] */
	UPROPERTY()
	FBoneReference FollowBone_DEPRECATED;
	/** [32] */
	UPROPERTY()
	TArray<FKawaiiPhysicsRootBoneSetting> AdditionalRootBones_DEPRECATED;
	/** [33] */
	UPROPERTY()
	float DummyBoneLength_DEPRECATED = 0.0f;
	/** [34] */
	UPROPERTY()
	EBoneForwardAxis BoneForwardAxis_DEPRECATED = EBoneForwardAxis::X_Positive;
	/** [35] */
	UPROPERTY()
	FKawaiiPhysicsSettings PhysicsSettings_DEPRECATED;
	/** [36] */
	UPROPERTY()
	TArray<FBoneShape> CustomShapes_DEPRECATED;
	/** [37] */
	UPROPERTY()
	bool bUseCurve_DEPRECATED = false;
	/** [38] */
	UPROPERTY()
	TArray<FBonePhysicsParams> PhysicsParamsPerBone_DEPRECATED;
	/** [39] */
	UPROPERTY()
	float TeleportDistanceThreshold_DEPRECATED = 300.0f;
	/** [40] */
	UPROPERTY()
	float TeleportRotationThreshold_DEPRECATED = 10.0f;
	/** [41] */
	UPROPERTY()
	EPlanarConstraint PlanarConstraint_DEPRECATED = EPlanarConstraint::None;
	/** [42] */
	UPROPERTY()
	TObjectPtr<UCurveFloat> DampingCurve_DEPRECATED = nullptr;
	/** [43] */
	UPROPERTY()
	TObjectPtr<UCurveFloat> WorldDampingLocationCurve_DEPRECATED = nullptr;
	/** [44] */
	UPROPERTY()
	TObjectPtr<UCurveFloat> WorldDampingRotationCurve_DEPRECATED = nullptr;
	/** [45] */
	UPROPERTY()
	TObjectPtr<UCurveFloat> StiffnessCurve_DEPRECATED = nullptr;
	/** [46] */
	UPROPERTY()
	TObjectPtr<UCurveFloat> RadiusCurve_DEPRECATED = nullptr;
	/** [47] */
	UPROPERTY()
	TObjectPtr<UCurveFloat> LimitAngleCurve_DEPRECATED = nullptr;
	/** [48] */
	UPROPERTY()
	FRuntimeFloatCurve DampingCurveData_DEPRECATED;
	/** [49] */
	UPROPERTY()
	FRuntimeFloatCurve StiffnessCurveData_DEPRECATED;
	/** [50] */
	UPROPERTY()
	FRuntimeFloatCurve WorldDampingLocationCurveData_DEPRECATED;
	/** [51] */
	UPROPERTY()
	FRuntimeFloatCurve WorldDampingRotationCurveData_DEPRECATED;
	/** [52] */
	UPROPERTY()
	FRuntimeFloatCurve RadiusCurveData_DEPRECATED;
	/** [53] */
	UPROPERTY()
	FRuntimeFloatCurve LimitAngleCurveData_DEPRECATED;
	/** [54] */
	UPROPERTY()
	FRuntimeFloatCurve LimitLinearCurveData_DEPRECATED;
	/** [55] */
	UPROPERTY()
	FRuntimeFloatCurve GravityCurveData_DEPRECATED;
	/** [56] */
	UPROPERTY()
	EXPBDComplianceType BoneConstraintGlobalComplianceType_DEPRECATED = EXPBDComplianceType::Leather;
	/** [57] */
	UPROPERTY()
	int32 BoneConstraintIterationCountBeforeCollision_DEPRECATED = 1;
	/** [58] */
	UPROPERTY()
	int32 BoneConstraintIterationCountAfterCollision_DEPRECATED = 1;
	/** [59] */
	UPROPERTY()
	bool bAutoAddChildDummyBoneConstraint_DEPRECATED = true;
	/** [60] */
	UPROPERTY()
	TArray<FModifyBoneConstraint> BoneConstraints_DEPRECATED;
	/** [61] */
	UPROPERTY()
	TObjectPtr<UKawaiiPhysicsBoneConstraintsDataAsset> BoneConstraintsDataAsset_DEPRECATED = nullptr;
	/** [62] */
	UPROPERTY()
	TArray<FModifyBoneConstraint> BoneConstraintsData_DEPRECATED;
	/** [63] */
	UPROPERTY()
	FVector Gravity_DEPRECATED = FVector::ZeroVector;
	/** [64] */
	UPROPERTY()
	bool bEnableWind_DEPRECATED = false;
	/** [65] */
	UPROPERTY()
	float WindScale_DEPRECATED = 1.0f;
	/** [66] */
	UPROPERTY()
	bool bEnableWaveAnim_DEPRECATED = false;
	/** [67] */
	UPROPERTY()
	FBoneReference WaveBeginBone_DEPRECATED;
	/** [68] */
	UPROPERTY()
	float WaveFrequncy_DEPRECATED = 1.0f;
	/** [69] */
	UPROPERTY()
	float WaveNum_DEPRECATED = 1.0f;
	/** [70] */
	UPROPERTY()
	FVector WaveDirection_DEPRECATED = FVector::ZeroVector;
	/** [71] */
	UPROPERTY()
	float WaveAmplitude_DEPRECATED = 0.0f;
	/** [72] */
	UPROPERTY()
	FRuntimeFloatCurve WaveAmplitudeCurveData_DEPRECATED;
	/** [73] */
	UPROPERTY()
	TArray<FInstancedStruct> ExternalForces_DEPRECATED;
	/** [74] */
	UPROPERTY()
	TArray<TObjectPtr<UKawaiiPhysics_CustomExternalForce>> CustomExternalForces_DEPRECATED;

	// ================= end of deprecated block =================

protected:
	/** [75] Component being driven, cached in PreUpdate for the worker thread. */
	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> ParentSkelComp = nullptr;

	/** [76] Component transform as of the previous evaluation. */
	UPROPERTY()
	FTransform PreSkelCompTransform;

	/** [77] */
	UPROPERTY()
	bool bInitPhysicsSettings = false;

	// --- editor-only; must stay after [77], see SCHEMA NOTE ---
#if WITH_EDITORONLY_DATA
	UPROPERTY()
	bool bEditing = false;

	UPROPERTY()
	double LastEvaluatedTime = 0.0;
#endif

	// --- runtime-only scratch ---

	/** Delta time of the previous pass, for the variable-timestep correction (game: node +0xA84). */
	float DeltaTimeOld = 0.0f;

	/** The wave animation's clock (game: node +0xA90): 0 at construction, += DeltaTime after every simulate pass
	 *  (warm-up passes included), never reset. Shared by every chain of the node. */
	float WaveTime = 0.0f;

	/** Set by ResetDynamics, consumed on the next evaluation. */
	bool bResetDynamics = false;

	/** LOD the node was last evaluated at; gates per-chain LOD thresholds. */
	int32 CachedLODLevel = 0;

	/** Cached transform of the parent component, for bUseParentCompBone limits. */
	FTransform ParentCompTransform = FTransform::Identity;

public:
	FAnimNode_KawaiiPhysics();
	/** Physics-asset bodies -> limits, shared with the Merge node (game 0x7671350 / 0x7671BE0). */
	static void BuildPhysicsAssetLimits(const UPhysicsAsset* PhysicsAsset, const FBoneContainer& RequiredBones,
	                                    TArray<FSphericalLimit>& SphericalLimitsData,
	                                    TArray<FCapsuleLimit>& CapsuleLimitsData, TArray<FBoxLimit>& BoxLimitsData);

	// FAnimNode_Base interface
	virtual void UpdateInternal(const FAnimationUpdateContext& Context) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual bool NeedsDynamicReset() const override { return true; }
	virtual void ResetDynamics(ETeleportType InTeleportType) override;
	// End of FAnimNode_Base interface

	// FAnimNode_SkeletalControlBase interface
	virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output,
	                                               TArray<FBoneTransform>& OutBoneTransforms) override;
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;
	virtual bool HasPreUpdate() const override;
	virtual void PreUpdate(const UAnimInstance* InAnimInstance) override;
	// End of FAnimNode_SkeletalControlBase interface

	/**
	 * Folds the deprecated flat properties into Chains[0].
	 *
	 * Called from the AnimGraph node's PostLoad. Only does anything when Chains
	 * is empty and a deprecated RootBone is present, so it is safe to call twice.
	 */
	void UpgradeDeprecatedProperties();

	/** Convenience for the Blueprint library; returns nullptr for a bad index. */
	FKawaiiPhysicsChain* GetChain(int32 ChainIndex)
	{
		return Chains.IsValidIndex(ChainIndex) ? &Chains[ChainIndex] : nullptr;
	}

	const FKawaiiPhysicsChain* GetChain(int32 ChainIndex) const
	{
		return Chains.IsValidIndex(ChainIndex) ? &Chains[ChainIndex] : nullptr;
	}

#if WITH_EDITORONLY_DATA
	bool IsRecentlyEvaluated() const { return (FPlatformTime::Seconds() - LastEvaluatedTime) < 0.1; }
#endif

	/** Maps a chain's forward-axis setting onto an axis of the given rotation. */
	static FVector GetBoneForwardVector(const FQuat& Rotation, EBoneForwardAxis ForwardAxis)
	{
		switch (ForwardAxis)
		{
		default:
		case EBoneForwardAxis::X_Positive:
			return Rotation.GetAxisX();
		case EBoneForwardAxis::X_Negative:
			return -Rotation.GetAxisX();
		case EBoneForwardAxis::Y_Positive:
			return Rotation.GetAxisY();
		case EBoneForwardAxis::Y_Negative:
			return -Rotation.GetAxisY();
		case EBoneForwardAxis::Z_Positive:
			return Rotation.GetAxisZ();
		case EBoneForwardAxis::Z_Negative:
			return -Rotation.GetAxisZ();
		}
	}

protected:
	// FAnimNode_SkeletalControlBase interface
	virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;
	// End of FAnimNode_SkeletalControlBase interface

	// --- setup, per chain ---
	void InitModifyBones(FKawaiiPhysicsChain& Chain, FComponentSpacePoseContext& Output,
	                     const FBoneContainer& BoneContainer);
	int32 AddModifyBone(FKawaiiPhysicsChain& Chain, TArray<FKawaiiPhysicsModifyBone>& InModifyBones,
	                    FComponentSpacePoseContext& Output, const FBoneContainer& BoneContainer,
	                    const FReferenceSkeleton& RefSkeleton, int32 BoneIndex,
	                    const TArray<FBoneReference>& InExcludeBones);
	int32 CollectChildBones(const FReferenceSkeleton& RefSkeleton, int32 ParentBoneIndex,
	                        TArray<int32>& Children) const;
	void CalcBoneLength(FKawaiiPhysicsChain& Chain, FKawaiiPhysicsModifyBone& Bone,
	                    const TArray<FTransform>& RefBonePose);
	void InitBoneConstraints(FKawaiiPhysicsChain& Chain);
	void ApplyBoneConstraintDataAsset(FKawaiiPhysicsChain& Chain, const FBoneContainer& RequiredBones);
	void MarkFixedAndFollowBones(FKawaiiPhysicsChain& Chain, const FReferenceSkeleton& RefSkeleton,
	                             const FBoneContainer& BoneContainer);
	void MarkWaveBones(FKawaiiPhysicsChain& Chain);

	// --- shared limits ---
	void ApplyLimitsDataAsset(const FBoneContainer& RequiredBones);
	void ApplyPhysicsAsset(const FBoneContainer& RequiredBones);
	void UpdateSphericalLimits(TArray<FSphericalLimit>& Limits, FComponentSpacePoseContext& Output,
	                           const FBoneContainer& BoneContainer, const FTransform& ComponentTransform);
	void UpdateCapsuleLimits(TArray<FCapsuleLimit>& Limits, FComponentSpacePoseContext& Output,
	                         const FBoneContainer& BoneContainer, const FTransform& ComponentTransform);
	void UpdateBoxLimits(TArray<FBoxLimit>& Limits, FComponentSpacePoseContext& Output,
	                     const FBoneContainer& BoneContainer, const FTransform& ComponentTransform);
	void UpdatePlanerLimits(TArray<FPlanarLimit>& Limits, FComponentSpacePoseContext& Output,
	                        const FBoneContainer& BoneContainer, const FTransform& ComponentTransform);

	// --- per-frame, per chain ---
	void UpdatePhysicsSettingsOfModifyBones(FKawaiiPhysicsChain& Chain);
	void UpdateModifyBonesPoseTransform(FKawaiiPhysicsChain& Chain, FComponentSpacePoseContext& Output,
	                                    const FBoneContainer& BoneContainer);
	void UpdateSkelCompMove(FKawaiiPhysicsChain& Chain, const FTransform& ComponentTransform);
	/** One pass over every chain, then DeltaTimeOld = DeltaTime and WaveTime += DeltaTime (game 0x767E3DF).
	 *  LODLevel < 0 runs every chain (the warm-up's LOD 0 pass). */
	void SimulatePass(FComponentSpacePoseContext& Output, const FTransform& ComponentTransform, int32 LODLevel);
	void SimulateModifyBones(FKawaiiPhysicsChain& Chain, FComponentSpacePoseContext& Output,
	                         const FTransform& ComponentTransform);
	void Simulate(FKawaiiPhysicsChain& Chain, FKawaiiPhysicsModifyBone& Bone, const FSceneInterface* Scene,
	              const FTransform& ComponentTransform, const FVector& GravityCS, const float& Exponent,
	              const USkeletalMeshComponent* SkelComp, FComponentSpacePoseContext& Output, bool bHasFixed);
	/** The bone's parent in the solve: its chain parent, or the chain's virtual parent for a simulated root. */
	static const FKawaiiPhysicsModifyBone* GetSimParent(const FKawaiiPhysicsChain& Chain,
	                                                    const FKawaiiPhysicsModifyBone& Bone);
	void FixTailStiffness(FKawaiiPhysicsChain& Chain, float Exponent);
	void FixTailLengths(FKawaiiPhysicsChain& Chain);
	FVector GetWaveOffset(const FKawaiiPhysicsChain& Chain, const FKawaiiPhysicsModifyBone& Bone, float Time,
	                      bool bHasFixed) const;

	// --- constraint resolution ---
	void AdjustByWorldCollision(FKawaiiPhysicsChain& Chain, FKawaiiPhysicsModifyBone& Bone,
	                            const USkeletalMeshComponent* OwningComp);
	void AdjustBySphereCollision(FKawaiiPhysicsModifyBone& Bone, TArray<FSphericalLimit>& Limits);
	void AdjustByCapsuleCollision(FKawaiiPhysicsModifyBone& Bone, TArray<FCapsuleLimit>& Limits);
	void AdjustByBoxCollision(FKawaiiPhysicsModifyBone& Bone, TArray<FBoxLimit>& Limits);
	void AdjustByPlanerCollision(FKawaiiPhysicsModifyBone& Bone, TArray<FPlanarLimit>& Limits);
	void AdjustByAngleLimit(FKawaiiPhysicsChain& Chain, FKawaiiPhysicsModifyBone& Bone,
	                        const FKawaiiPhysicsModifyBone& ParentBone);
	void AdjustByLinearLimit(FKawaiiPhysicsModifyBone& Bone);
	void AdjustByPlanarConstraint(FKawaiiPhysicsChain& Chain, FKawaiiPhysicsModifyBone& Bone,
	                              const FKawaiiPhysicsModifyBone& ParentBone);
	void AdjustByBoneConstraints(FKawaiiPhysicsChain& Chain, int32 IterationCount);
	void AdjustByRelativeRotationLimit(FKawaiiPhysicsChain& Chain, FKawaiiPhysicsModifyBone& Bone,
	                                   const FKawaiiPhysicsModifyBone& ParentBone);

	// --- output ---
	void ApplySimulateResult(FKawaiiPhysicsChain& Chain, FComponentSpacePoseContext& Output,
	                         const FBoneContainer& BoneContainer, TArray<FBoneTransform>& OutBoneTransforms);

	FVector GetWindVelocity(const FKawaiiPhysicsChain& Chain, const FSceneInterface* Scene,
	                        const FTransform& ComponentTransform, const FKawaiiPhysicsModifyBone& Bone) const;

#if ENABLE_ANIM_DEBUG
	void AnimDrawDebug(const FComponentSpacePoseContext& Output);
#endif
};
