// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License
//
// The five force types here match the game's schema exactly as-is; only the
// C++ signatures changed, to carry the owning chain (these are plain virtuals,
// not UFUNCTIONs, so they are not part of the serialized schema).

#pragma once
#include "AnimNode_KawaiiPhysics.h"
#include "SceneManagement.h"
#include "Curves/CurveVector.h"
#include "KawaiiPhysicsExternalForce.generated.h"

/** Frame the force vector is authored in. */
UENUM(BlueprintType)
enum class EExternalForceSpace : uint8
{
	/** Component space: moving the mesh does not itself generate force. */
	ComponentSpace,
	/** World space: moving the mesh generates force. */
	WorldSpace,
	/** Space of the bone being simulated. */
	BoneSpace,
};

/** How a curve-driven force samples the interval since the last frame. */
UENUM(BlueprintType)
enum class EExternalForceCurveEvaluateType : uint8
{
	Single,
	Average,
	Max,
	Min
};


///////////////////////////////////////////////////////////////////////////////
// Base
///////////////////////////////////////////////////////////////////////////////

USTRUCT(BlueprintType)
struct KAWAIIPHYSICS_API FKawaiiPhysics_ExternalForce
{
	GENERATED_BODY()

	/** [0] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayPriority = 1), Category = "KawaiiPhysics|ExternalForce")
	bool bIsEnabled = true;

	/** [1] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayPriority = 1), Category = "KawaiiPhysics|ExternalForce")
	bool bDrawDebug = false;

	/** [2] If non-empty, only these bones receive the force. */
	UPROPERTY(EditAnywhere, meta = (DisplayPriority = 1), Category = "KawaiiPhysics|ExternalForce")
	TArray<FBoneReference> ApplyBoneFilter;

	/** [3] These bones never receive the force. */
	UPROPERTY(EditAnywhere, meta = (DisplayPriority = 1), Category = "KawaiiPhysics|ExternalForce")
	TArray<FBoneReference> IgnoreBoneFilter;

	/** [4] */
	UPROPERTY(EditAnywhere, meta = (DisplayPriority = 1, EditCondition = bCanSelectForceSpace, EditConditionHides),
		Category = "KawaiiPhysics|ExternalForce")
	EExternalForceSpace ExternalForceSpace = EExternalForceSpace::WorldSpace;

	/** [5] Re-rolled every frame in PreApply, so gusts do not look mechanical. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayPriority = 1), Category = "KawaiiPhysics|ExternalForce")
	FFloatInterval RandomForceScaleRange = FFloatInterval(1.0f, 1.0f);

	/** [6] Set when the force was pushed in by an AnimNotify, so it can be matched on removal. */
	UPROPERTY()
	TObjectPtr<UObject> ExternalOwner;

	/** [7] Remove this force from the chain after a single application. */
	UPROPERTY()
	bool bIsOneShot = false;

	// Debug members are deliberately NOT UPROPERTY: they would otherwise appear in
	// the schema in Development builds and shift every index after them.
#if ENABLE_ANIM_DEBUG
	float DebugArrowLength = 5.0f;
	float DebugArrowSize = 1.0f;
	FVector DebugArrowOffset = FVector::Zero();
	TMap<FName, FVector> BoneForceMap;
#endif

protected:
	/** [8] */
	UPROPERTY()
	float RandomizedForceScale = 0.0f;

	/** [9] Force resolved for the current frame. */
	UPROPERTY()
	FVector Force = FVector::Zero();

	/** [10] */
	UPROPERTY()
	FTransform ComponentTransform;

	/** [11] Cleared by force types that are inherently world-space. */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "KawaiiPhysics|ExternalForce")
	bool bCanSelectForceSpace = true;

public:
	virtual ~FKawaiiPhysics_ExternalForce() = default;

	virtual void Initialize(const FAnimationInitializeContext& Context)
	{
	}

	virtual void PreApply(FAnimNode_KawaiiPhysics& Node, FKawaiiPhysicsChain& Chain,
	                      const USkeletalMeshComponent* SkelComp)
	{
		ComponentTransform = SkelComp->GetComponentTransform();
		RandomizedForceScale = FMath::RandRange(RandomForceScaleRange.Min, RandomForceScaleRange.Max);
	}

	virtual void Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node, FKawaiiPhysicsChain& Chain,
	                   const FComponentSpacePoseContext& PoseContext, const FTransform& BoneTM = FTransform::Identity)
	{
	}

	virtual void PostApply(FAnimNode_KawaiiPhysics& Node, FKawaiiPhysicsChain& Chain)
	{
		if (bIsOneShot)
		{
			Chain.ExternalForceSettings.ExternalForces.RemoveAll([&](FInstancedStruct& InstancedStruct)
			{
				const auto* ExternalForcePtr = InstancedStruct.GetMutablePtr<FKawaiiPhysics_ExternalForce>();
				return ExternalForcePtr == this;
			});
		}
	}

	virtual bool IsDebugEnabled(bool bInPersona = false)
	{
		if (bInPersona)
		{
			return bDrawDebug && bIsEnabled;
		}

#if ENABLE_ANIM_DEBUG
		if (CVarAnimNodeKawaiiPhysicsDebug.GetValueOnAnyThread())
		{
			return bDrawDebug && bIsEnabled;
		}
#endif

		return false;
	}

#if ENABLE_ANIM_DEBUG
	virtual void AnimDrawDebug(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
	                           const FComponentSpacePoseContext& PoseContext)
	{
		if (!IsDebugEnabled() || Force.IsZero())
		{
			return;
		}

		const FVector* BoneForce = BoneForceMap.Find(Bone.BoneRef.BoneName);
		if (!BoneForce)
		{
			return;
		}

		const auto AnimInstanceProxy = PoseContext.AnimInstanceProxy;
		const FVector ModifyRootBoneLocationWS =
			AnimInstanceProxy->GetComponentTransform().TransformPosition(Bone.Location);

		AnimInstanceProxy->AnimDrawDebugDirectionalArrow(
			ModifyRootBoneLocationWS + DebugArrowOffset,
			ModifyRootBoneLocationWS + DebugArrowOffset + BoneForce->GetSafeNormal() * DebugArrowLength,
			DebugArrowSize, FColor::Red, false, 0.f, 2);
	}
#endif

#if WITH_EDITOR
	virtual void AnimDrawDebugForEditMode(const FKawaiiPhysicsModifyBone& ModifyBone,
	                                      const FAnimNode_KawaiiPhysics& Node, FPrimitiveDrawInterface* PDI)
	{
#if ENABLE_ANIM_DEBUG
		if (IsDebugEnabled(true) && CanApply(ModifyBone) && !Force.IsNearlyZero() &&
			BoneForceMap.Contains(ModifyBone.BoneRef.BoneName))
		{
			const FTransform ArrowTransform = FTransform(
				BoneForceMap.Find(ModifyBone.BoneRef.BoneName)->GetSafeNormal().ToOrientationRotator(),
				ModifyBone.Location + DebugArrowOffset);
			DrawDirectionalArrow(PDI, ArrowTransform.ToMatrixNoScale(), FColor::Red, DebugArrowLength, DebugArrowSize,
			                     SDPG_Foreground, 1.0f);
		}
#endif
	}
#endif

protected:
	bool CanApply(const FKawaiiPhysicsModifyBone& Bone) const
	{
		if (!ApplyBoneFilter.IsEmpty() && !ApplyBoneFilter.Contains(Bone.BoneRef))
		{
			return false;
		}

		if (!IgnoreBoneFilter.IsEmpty() && IgnoreBoneFilter.Contains(Bone.BoneRef))
		{
			return false;
		}

		return true;
	}
};

///////////////////////////////////////////////////////////////////////////////
// Basic
///////////////////////////////////////////////////////////////////////////////

USTRUCT(BlueprintType, DisplayName = "Basic")
struct KAWAIIPHYSICS_API FKawaiiPhysics_ExternalForce_Basic : public FKawaiiPhysics_ExternalForce
{
	GENERATED_BODY()

	/** [0] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KawaiiPhysics|ExternalForce")
	FVector ForceDir = FVector::Zero();

	/** [1] Sampled at the bone's LengthRateFromRoot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KawaiiPhysics|ExternalForce")
	FRuntimeFloatCurve ForceRateByBoneLengthRate;

	/** [2] Seconds between pulses; 0 applies the force continuously. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KawaiiPhysics|ExternalForce")
	float Interval = 0.0f;

	virtual void PreApply(FAnimNode_KawaiiPhysics& Node, FKawaiiPhysicsChain& Chain,
	                      const USkeletalMeshComponent* SkelComp) override;
	virtual void Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node, FKawaiiPhysicsChain& Chain,
	                   const FComponentSpacePoseContext& PoseContext,
	                   const FTransform& BoneTM = FTransform::Identity) override;

private:
	/** [3] */
	UPROPERTY()
	float Time = 0.0f;

	/** [4] */
	UPROPERTY()
	float PrevTime = 0.0f;
};

///////////////////////////////////////////////////////////////////////////////
// Gravity
///////////////////////////////////////////////////////////////////////////////

USTRUCT(BlueprintType, DisplayName = "Gravity")
struct KAWAIIPHYSICS_API FKawaiiPhysics_ExternalForce_Gravity : public FKawaiiPhysics_ExternalForce
{
	GENERATED_BODY()

	FKawaiiPhysics_ExternalForce_Gravity()
	{
		bCanSelectForceSpace = false;
		ExternalForceSpace = EExternalForceSpace::WorldSpace;
	}

	/** [0] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KawaiiPhysics|ExternalForce")
	FRuntimeFloatCurve ForceRateByBoneLengthRate;

	/** [1] Follow the owning Character's custom gravity direction (UE 5.4+). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KawaiiPhysics|ExternalForce")
	bool bUseCharacterGravityDirection = false;

	/** [2] Multiply by the owning Character's GravityScale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KawaiiPhysics|ExternalForce")
	bool bUseCharacterGravityScale = false;

	/** [3] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bUseOverrideGravityDirection"),
		Category = "KawaiiPhysics|ExternalForce")
	FVector OverrideGravityDirection = FVector::Zero();

	/** [4] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (InlineEditConditionToggle),
		Category = "KawaiiPhysics|ExternalForce")
	bool bUseOverrideGravityDirection = false;

	virtual void PreApply(FAnimNode_KawaiiPhysics& Node, FKawaiiPhysicsChain& Chain,
	                      const USkeletalMeshComponent* SkelComp) override;
	virtual void Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node, FKawaiiPhysicsChain& Chain,
	                   const FComponentSpacePoseContext& PoseContext,
	                   const FTransform& BoneTM = FTransform::Identity) override;
};

///////////////////////////////////////////////////////////////////////////////
// Curve
///////////////////////////////////////////////////////////////////////////////

USTRUCT(BlueprintType, DisplayName = "Curve")
struct KAWAIIPHYSICS_API FKawaiiPhysics_ExternalForce_Curve : public FKawaiiPhysics_ExternalForce
{
	GENERATED_BODY()

	/** [0] X axis is time, Y is force. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (XAxisName = "Time", YAxisName = "Force"),
		Category = "KawaiiPhysics|ExternalForce")
	FRuntimeVectorCurve ForceCurve;

	/** [1] Anything but Single subdivides the frame and combines the samples. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KawaiiPhysics|ExternalForce")
	EExternalForceCurveEvaluateType CurveEvaluateType = EExternalForceCurveEvaluateType::Single;

	/** [2] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite,
		meta = (EditCondition = "CurveEvaluateType!=EExternalForceCurveEvaluateType::Single"),
		Category = "KawaiiPhysics|ExternalForce")
	int SubstepCount = 10;

	/** [3] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KawaiiPhysics|ExternalForce")
	float TimeScale = 1.0f;

	/** [4] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KawaiiPhysics|ExternalForce")
	FRuntimeFloatCurve ForceRateByBoneLengthRate;

private:
	/** [5] */
	UPROPERTY()
	float Time = 0.0f;

	/** [6] */
	UPROPERTY()
	float PrevTime = 0.0f;

	/** [7] Wraps Time, so the curve loops. */
	UPROPERTY()
	float MaxCurveTime = 0.0f;

public:
	void InitMaxCurveTime();

	virtual void Initialize(const FAnimationInitializeContext& Context) override;
	virtual void PreApply(FAnimNode_KawaiiPhysics& Node, FKawaiiPhysicsChain& Chain,
	                      const USkeletalMeshComponent* SkelComp) override;
	virtual void Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node, FKawaiiPhysicsChain& Chain,
	                   const FComponentSpacePoseContext& PoseContext,
	                   const FTransform& BoneTM = FTransform::Identity) override;
};

///////////////////////////////////////////////////////////////////////////////
// Wind
///////////////////////////////////////////////////////////////////////////////

USTRUCT(BlueprintType, DisplayName = "Wind")
struct KAWAIIPHYSICS_API FKawaiiPhysics_ExternalForce_Wind : public FKawaiiPhysics_ExternalForce
{
	GENERATED_BODY()

	FKawaiiPhysics_ExternalForce_Wind()
	{
		bCanSelectForceSpace = false;
		ExternalForceSpace = EExternalForceSpace::WorldSpace;
	}

	/** [0] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KawaiiPhysics|ExternalForce")
	FRuntimeFloatCurve ForceRateByBoneLengthRate;

private:
	/** [1] Cached in PreApply; the wind sources live on the scene. */
	UPROPERTY()
	TObjectPtr<UWorld> World;

public:
	virtual void PreApply(FAnimNode_KawaiiPhysics& Node, FKawaiiPhysicsChain& Chain,
	                      const USkeletalMeshComponent* SkelComp) override;
	virtual void Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node, FKawaiiPhysicsChain& Chain,
	                   const FComponentSpacePoseContext& PoseContext,
	                   const FTransform& BoneTM = FTransform::Identity) override;
};
