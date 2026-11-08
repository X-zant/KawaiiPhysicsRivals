// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License
//
// See the SCHEMA NOTE at the top of AnimNode_KawaiiPhysics.h. The same rule
// applies here: UPROPERTY declaration order is the serialization schema.

#pragma once

#include "CoreMinimal.h"
#include "AnimNode_KawaiiPhysics.h"

#include "AnimNode_KawaiiPhysicsMerge.generated.h"

/**
 * One chain's worth of setup inside a Merge node.
 *
 * The Merge node exists so several chains can share a single collision pass and
 * a single constraint solve, which is cheaper than stacking separate nodes.
 * Consequently this struct holds only the per-chain bone setup: the limits,
 * curves, external forces and constraint settings all live on the node.
 */
USTRUCT(BlueprintType)
struct KAWAIIPHYSICS_API FSingleNodeParameter
{
	GENERATED_BODY()

	/** [0] */
	UPROPERTY(EditAnywhere, Category = "Bones")
	FBoneReference RootBone;

	/** [1] */
	UPROPERTY(EditAnywhere, Category = "Bones")
	TArray<FBoneReference> ExcludeBones;

	/** [2] */
	UPROPERTY(EditAnywhere, Category = "Bones")
	bool bRootBoneSimulate = false;

	/** [3] */
	UPROPERTY(EditAnywhere, Category = "Bones")
	bool bShouldFixTailBone = false;

	/** [4] */
	UPROPERTY(EditAnywhere, Category = "Bones")
	FBoneReference FixedBone;

	/** [5] */
	UPROPERTY(EditAnywhere, Category = "Bones")
	FBoneReference FollowBone;

	/** [6] */
	UPROPERTY(EditAnywhere, Category = "Bones")
	TArray<FKawaiiPhysicsRootBoneSetting> AdditionalRootBones;

	/** [7] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bones", meta = (ClampMin = "0"))
	float DummyBoneLength = 0.0f;

	/** [8] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bones")
	EBoneForwardAxis BoneForwardAxis = EBoneForwardAxis::X_Positive;

	/** [9] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", meta = (DisplayPriority = 0))
	FKawaiiPhysicsSettings PhysicsSettings;

	/** [10] */
	UPROPERTY(EditAnywhere, Category = "Physics Settings")
	TArray<FBoneShape> CustomShapes;

	/** [11] */
	UPROPERTY(BlueprintReadWrite, Category = "Bones")
	TArray<FKawaiiPhysicsModifyBone> ModifyBones;

	/** [12] */
	UPROPERTY()
	float TotalBoneLength = 0.0f;

	/** [13] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
	int32 AutoConfiguredLODThreshold = -1;

	/** [14] */
	UPROPERTY(Transient)
	TEnumAsByte<ENetRole> CachedOwnerNetRole = ROLE_None;

	// --- runtime-only scratch; no schema index ---

	/** The skeleton parent of RootBone (game: param +0xB8) and the virtual parent a simulated root is solved
	 *  against (game: param +0xD0). */
	FBoneReference RootParentRef;
	FKawaiiPhysicsModifyBone VirtualParentBone;
	bool bHasVirtualParent = false;

	/** Fixed/follow bone state (game: param +0x260 follow ref, +0x280 follow target, +0x2E0 fixed-relative-to-
	 *  follow reference transform). */
	FBoneReference FollowBoneRef;
	FTransform FollowTargetTransform = FTransform::Identity;
	FTransform FixedRefRelToFollow = FTransform::Identity;

	/** Cleared (e.g. by the editor) to force the chain to be rebuilt. */
	bool bInitialized = false;

	/** The game's rule: a chain runs when its threshold is -1 or >= the current LOD. */
	bool IsEnabledForLOD(int32 InLODLevel) const
	{
		return AutoConfiguredLODThreshold == -1 || AutoConfiguredLODThreshold >= InLODLevel;
	}
};

/**
 * Simulates several bone chains against one shared set of limits.
 *
 * Behaviourally this is the same solver as FAnimNode_KawaiiPhysics; the split is
 * about cost. Where the regular node runs collision and constraints once per
 * chain, this node gathers every chain's bones first and resolves them together,
 * so a character with many short chains pays for one pass instead of N.
 */
USTRUCT(BlueprintType)
struct KAWAIIPHYSICS_API FAnimNode_KawaiiPhysicsMerge : public FAnimNode_SkeletalControlBase
{
	GENERATED_USTRUCT_BODY()

	/** [0] */
	UPROPERTY(EditAnywhere, Category = "Bones")
	TArray<FSingleNodeParameter> Nodes;

	/** [1] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings")
	float TeleportDistanceThreshold = 300.0f;

	/** [2] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings")
	float TeleportRotationThreshold = 10.0f;

	/** [3] */
	UPROPERTY(EditAnywhere, Category = "Physics Settings", meta = (EditCondition = "OverrideTargetFramerate"))
	int32 TargetFramerate = 60;

	/** [4] */
	UPROPERTY(EditAnywhere, Category = "Physics Settings", meta = (InlineEditConditionToggle))
	bool OverrideTargetFramerate = false;

	/** [5] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings",
		meta = (PinHiddenByDefault, EditCondition = "bNeedWarmUp", ClampMin = "0"))
	int32 WarmUpFrames = 0;

	/** [6] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings",
		meta = (PinHiddenByDefault, EditCondition = "bNeedWarmUp"))
	bool bUseWarmUpWhenResetDynamics = true;

	/** [7] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings",
		meta = (PinHiddenByDefault, InlineEditConditionToggle))
	bool bNeedWarmUp = false;

	/** [8] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", meta = (PinHiddenByDefault))
	EPlanarConstraint PlanarConstraint = EPlanarConstraint::None;

	/** [9] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", AdvancedDisplay,
		meta = (PinHiddenByDefault))
	bool bUpdatePhysicsSettingsInGame = true;

	/** [10] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", AdvancedDisplay,
		meta = (PinHiddenByDefault))
	bool ResetBoneTransformWhenBoneNotFound = false;

	/** [11] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", AdvancedDisplay,
		meta = (DisplayName = "Damping Rate by Bone Length Rate"))
	FRuntimeFloatCurve DampingCurveData;

	/** [12] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", AdvancedDisplay,
		meta = (DisplayName = "Stiffness Rate by Bone Length Rate"))
	FRuntimeFloatCurve StiffnessCurveData;

	/** [13] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", AdvancedDisplay,
		meta = (DisplayName = "World Damping Location Rate by Bone Length Rate"))
	FRuntimeFloatCurve WorldDampingLocationCurveData;

	/** [14] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", AdvancedDisplay,
		meta = (DisplayName = "World Damping Rotation Rate by Bone Length Rate"))
	FRuntimeFloatCurve WorldDampingRotationCurveData;

	/** [15] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", AdvancedDisplay,
		meta = (DisplayName = "Radius Rate by Bone Length Rate"))
	FRuntimeFloatCurve RadiusCurveData;

	/** [16] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", AdvancedDisplay,
		meta = (DisplayName = "LimitAngle Rate by Bone Length Rate"))
	FRuntimeFloatCurve LimitAngleCurveData;

	/** [17] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings",
		meta = (DisplayName = "LimitLinear Rate by Bone Length Rate"))
	FRuntimeFloatCurve LimitLinearCurveData;

	/** [18] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings",
		meta = (DisplayName = "Gravity Rate by Bone Length Rate"))
	FRuntimeFloatCurve GravityCurveData;

	/** [19] */
	UPROPERTY(EditAnywhere, Category = "Limits")
	TArray<FSphericalLimit> SphericalLimits;

	/** [20] */
	UPROPERTY(EditAnywhere, Category = "Limits")
	TArray<FCapsuleLimit> CapsuleLimits;

	/** [21] */
	UPROPERTY(EditAnywhere, Category = "Limits")
	TArray<FBoxLimit> BoxLimits;

	/** [22] */
	UPROPERTY(EditAnywhere, Category = "Limits")
	TArray<FPlanarLimit> PlanarLimits;

	/** [23] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limits", meta = (PinHiddenByDefault))
	TObjectPtr<UKawaiiPhysicsLimitsDataAsset> LimitsDataAsset = nullptr;

	/** [24] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limits", meta = (PinHiddenByDefault))
	TObjectPtr<UPhysicsAsset> PhysicsAssetForLimits = nullptr;

	/** [25] */
	UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category = "Limits")
	TArray<FSphericalLimit> SphericalLimitsData;

	/** [26] */
	UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category = "Limits")
	TArray<FCapsuleLimit> CapsuleLimitsData;

	/** [27] */
	UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category = "Limits")
	TArray<FBoxLimit> BoxLimitsData;

	/** [28] */
	UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category = "Limits")
	TArray<FPlanarLimit> PlanarLimitsData;

	/** [29] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bone Constraint")
	EXPBDComplianceType BoneConstraintGlobalComplianceType = EXPBDComplianceType::Leather;

	/** [30] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bone Constraint")
	int32 BoneConstraintIterationCountBeforeCollision = 1;

	/** [31] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bone Constraint")
	int32 BoneConstraintIterationCountAfterCollision = 1;

	/** [32] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bone Constraint")
	bool bAutoAddChildDummyBoneConstraint = true;

	/** [33] */
	UPROPERTY(EditAnywhere, Category = "Bone Constraint", meta = (TitleProperty = "{Bone1} - {Bone2}"))
	TArray<FModifyBoneConstraint> BoneConstraints;

	/** [34] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bone Constraint")
	TObjectPtr<UKawaiiPhysicsBoneConstraintsDataAsset> BoneConstraintsDataAsset = nullptr;

	/** [35] */
	UPROPERTY(VisibleAnywhere, Category = "Bone Constraint", AdvancedDisplay,
		meta = (TitleProperty = "{Bone1} - {Bone2}"))
	TArray<FModifyBoneConstraint> BoneConstraintsData;

	/** [36] BoneConstraints + BoneConstraintsData, resolved against the merged bone list. */
	UPROPERTY()
	TArray<FModifyBoneConstraint> MergedBoneConstraints;

	/** [37] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ExternalForce", meta = (DisplayName = "External Force"))
	FVector Gravity = FVector::ZeroVector;

	/** [38] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ExternalForce", meta = (InlineEditConditionToggle))
	bool bEnableWind = false;

	/** [39] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ExternalForce", meta = (EditCondition = "bEnableWind"))
	float WindScale = 1.0f;

	/** [40] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ExternalForce",
		meta = (BaseStruct = "/Script/KawaiiPhysics.KawaiiPhysics_ExternalForce", ExcludeBaseStruct))
	TArray<FInstancedStruct> ExternalForces;

	/** [41] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = "ExternalForce")
	TArray<TObjectPtr<UKawaiiPhysics_CustomExternalForce>> CustomExternalForces;

	/** [42] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Collision", meta = (PinHiddenByDefault))
	bool bAllowWorldCollision = false;

	/** [43] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Collision",
		meta = (PinHiddenByDefault, InlineEditConditionToggle))
	bool bOverrideCollisionParams = false;

	/** [44] */
	UPROPERTY(EditAnywhere, Category = "World Collision",
		meta = (PinHiddenByDefault, EditCondition = "bOverrideCollisionParams",
			DisplayName = "Override SkelComp Collision Params"))
	FBodyInstance CollisionChannelSettings;

	/** [45] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Collision",
		meta = (PinHiddenByDefault, EditCondition = "bAllowWorldCollision"))
	bool bIgnoreSelfComponent = true;

	/** [46] */
	UPROPERTY(EditAnywhere, Category = "World Collision", meta = (EditCondition = "!bIgnoreSelfComponent"))
	TArray<FBoneReference> IgnoreBones;

	/** [47] */
	UPROPERTY(EditAnywhere, Category = "World Collision", meta = (EditCondition = "!bIgnoreSelfComponent"))
	TArray<FName> IgnoreBoneNamePrefix;

	/** [48] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag")
	FGameplayTag KawaiiPhysicsTag;

	/** [49] */
	UPROPERTY(BlueprintReadOnly, Category = "KawaiiPhysics")
	float DeltaTime = 0.0f;

protected:
	/** [50] */
	UPROPERTY()
	FTransform PreSkelCompTransform;

	/** [51] */
	UPROPERTY()
	bool bInitPhysicsSettings = false;

	// --- editor-only; must stay after [51], see SCHEMA NOTE ---
#if WITH_EDITORONLY_DATA
	UPROPERTY()
	bool bEditing = false;

	UPROPERTY()
	double LastEvaluatedTime = 0.0;
#endif

	// --- runtime-only scratch ---

	FVector SkelCompMoveVector = FVector::ZeroVector;
	FQuat SkelCompMoveRotation = FQuat::Identity;
	float DeltaTimeOld = 0.0f;
	bool bResetDynamics = false;
	int32 CachedLODLevel = 0;

	/**
	 * Cached in PreUpdate; the worker thread cannot walk back to the component.
	 *
	 * Deliberately a raw pointer rather than TObjectPtr: the game's schema has no
	 * property here, so it cannot be a UPROPERTY, and a TObjectPtr that nothing
	 * is tracking would only look safer than it is. Refreshed every PreUpdate and
	 * read only within the same frame's evaluation.
	 */
	USkeletalMeshComponent* ParentSkelComp = nullptr;

public:
	FAnimNode_KawaiiPhysicsMerge();

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

#if WITH_EDITORONLY_DATA
	bool IsRecentlyEvaluated() const { return (FPlatformTime::Seconds() - LastEvaluatedTime) < 0.1; }
#endif

protected:
	// FAnimNode_SkeletalControlBase interface
	virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;
	// End of FAnimNode_SkeletalControlBase interface

	void InitModifyBones(FSingleNodeParameter& Param, FComponentSpacePoseContext& Output,
	                     const FBoneContainer& BoneContainer);
	int32 AddModifyBone(FSingleNodeParameter& Param, TArray<FKawaiiPhysicsModifyBone>& InModifyBones,
	                    FComponentSpacePoseContext& Output, const FBoneContainer& BoneContainer,
	                    const FReferenceSkeleton& RefSkeleton, int32 BoneIndex,
	                    const TArray<FBoneReference>& InExcludeBones);
	void CalcBoneLength(FSingleNodeParameter& Param, FKawaiiPhysicsModifyBone& Bone,
	                    const TArray<FTransform>& RefBonePose);

	void UpdatePhysicsSettingsOfModifyBones(FSingleNodeParameter& Param);
	void UpdateModifyBonesPoseTransform(FSingleNodeParameter& Param, FComponentSpacePoseContext& Output,
	                                    const FBoneContainer& BoneContainer);
	void UpdateSkelCompMove(const FTransform& ComponentTransform);
	void SimulateModifyBones(FSingleNodeParameter& Param, FComponentSpacePoseContext& Output,
	                         const FTransform& ComponentTransform);
	void ApplySimulateResult(FSingleNodeParameter& Param, FComponentSpacePoseContext& Output,
	                         const FBoneContainer& BoneContainer, TArray<FBoneTransform>& OutBoneTransforms);
	/** One pass over every chain, then DeltaTimeOld = DeltaTime (game 0x7680832). LODLevel < 0: every chain. */
	void SimulatePass(FComponentSpacePoseContext& Output, const FTransform& ComponentTransform, int32 LODLevel);
	static const FKawaiiPhysicsModifyBone* GetSimParent(const FSingleNodeParameter& Param,
	                                                    const FKawaiiPhysicsModifyBone& Bone);
};
