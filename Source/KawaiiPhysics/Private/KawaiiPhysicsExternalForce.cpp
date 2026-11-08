// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License

#include "KawaiiPhysicsExternalForce.h"

#include "Animation/AnimInstanceProxy.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "SceneInterface.h"

// Every force below reproduces upstream v1.18.0 (the game's merge point is at
// or before it). Points worth knowing because they are not what you might
// guess:
//  - Basic, Curve and Wind displace by Force * dt -- ONE power of dt. Only
//    Gravity uses 0.5 * Force * dt^2.
//  - World-space forces are converted to component space once, in PreApply.
//    Bone-space forces are carried into the bone's frame in Apply.
//  - Gravity's magnitude comes from RandomForceScaleRange; the direction is
//    used as authored (not normalised). With bUseCharacterGravityScale it is
//    further multiplied by the movement component's GravityZ, which is
//    negative -- upstream's behaviour, reproduced as is.

namespace
{
	/** Samples ForceRateByBoneLengthRate, leaving the force alone if unauthored. */
	float EvalForceRate(const FRuntimeFloatCurve& CurveData, float LengthRate)
	{
		const FRichCurve* Curve = CurveData.GetRichCurveConst();
		return (Curve && Curve->GetNumKeys() > 0) ? Curve->Eval(LengthRate) : 1.0f;
	}

	/** Basic and Curve share upstream's apply: per-space, one power of dt. */
	void ApplyLinearForce(const FVector& Force, EExternalForceSpace Space, float ForceRate,
	                      FKawaiiPhysicsModifyBone& Bone, const FTransform& BoneTM, float DeltaTime,
	                      TMap<FName, FVector>& BoneForceMap)
	{
		if (Space == EExternalForceSpace::BoneSpace)
		{
			const FVector BoneForce = BoneTM.TransformVector(Force);
			Bone.Location += BoneForce * ForceRate * DeltaTime;
			BoneForceMap.Add(Bone.BoneRef.BoneName, BoneForce * ForceRate);
		}
		else
		{
			Bone.Location += Force * ForceRate * DeltaTime;
			BoneForceMap.Add(Bone.BoneRef.BoneName, Force * ForceRate);
		}
	}
}

void FKawaiiPhysics_ExternalForce::PostApply(FAnimNode_KawaiiPhysics& Node, FKawaiiPhysicsChain& Chain)
{
	if (!bIsOneShot)
	{
		return;
	}

	const FKawaiiPhysics_ExternalForce* Self = this;
	for (FKawaiiPhysicsChain& Each : Node.Chains)
	{
		Each.ExternalForceSettings.ExternalForces.RemoveAll([Self](const FInstancedStruct& InstancedStruct)
		{
			return InstancedStruct.GetPtr<FKawaiiPhysics_ExternalForce>() == Self;
		});
	}
	// NOTE: `this` may be destroyed now; nothing below touches it.
}

///////////////////////////////////////////////////////////////////////////////
// Basic
///////////////////////////////////////////////////////////////////////////////

void FKawaiiPhysics_ExternalForce_Basic::PreApply(FAnimNode_KawaiiPhysics& Node, FKawaiiPhysicsChain& Chain,
                                                  const USkeletalMeshComponent* SkelComp)
{
	FKawaiiPhysics_ExternalForce::PreApply(Node, Chain, SkelComp);

	PrevTime = Time;
	Time += Node.DeltaTime;

	if (Interval > 0.0f)
	{
		// Fires on the frame the accumulated time passes the interval, then
		// keeps the remainder.
		if (Time > Interval)
		{
			Force = ForceDir * RandomizedForceScale;
			Time = FMath::Fmod(Time, Interval);
		}
		else
		{
			Force = FVector::ZeroVector;
		}
	}
	else
	{
		Force = ForceDir * RandomizedForceScale;
	}

	if (ExternalForceSpace == EExternalForceSpace::WorldSpace)
	{
		Force = ComponentTransform.InverseTransformVector(Force);
	}

	BoneForceMap.Reset();
}

void FKawaiiPhysics_ExternalForce_Basic::Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
                                       FKawaiiPhysicsChain& Chain,
                                       const FComponentSpacePoseContext& PoseContext, const FTransform& BoneTM)
{
	ApplyImpl(Bone, Node.DeltaTime, BoneTM);
}

void FKawaiiPhysics_ExternalForce_Basic::ApplyMerge(FKawaiiPhysicsModifyBone& Bone, float InDeltaTime,
                                       const FTransform& BoneTM)
{
	ApplyImpl(Bone, InDeltaTime, BoneTM);
}

void FKawaiiPhysics_ExternalForce_Basic::ApplyImpl(FKawaiiPhysicsModifyBone& Bone, float InDeltaTime,
                                       const FTransform& BoneTM)
{
	if (!CanApply(Bone))
	{
		return;
	}

	ApplyLinearForce(Force, ExternalForceSpace, EvalForceRate(ForceRateByBoneLengthRate, Bone.LengthRateFromRoot),
	                 Bone, BoneTM, InDeltaTime, BoneForceMap);
}

///////////////////////////////////////////////////////////////////////////////
// Gravity
///////////////////////////////////////////////////////////////////////////////

void FKawaiiPhysics_ExternalForce_Gravity::PreApply(FAnimNode_KawaiiPhysics& Node, FKawaiiPhysicsChain& Chain,
                                                    const USkeletalMeshComponent* SkelComp)
{
	FKawaiiPhysics_ExternalForce::PreApply(Node, Chain, SkelComp);

	Force = bUseOverrideGravityDirection ? OverrideGravityDirection : FVector(0, 0, -1.0f);

	if (const ACharacter* Character = SkelComp ? Cast<ACharacter>(SkelComp->GetOwner()) : nullptr)
	{
		if (bUseCharacterGravityDirection)
		{
			Force = Character->GetGravityDirection();
		}

		if (bUseCharacterGravityScale)
		{
			if (const UCharacterMovementComponent* CharacterMovementComponent = Character->GetCharacterMovement())
			{
				Force *= CharacterMovementComponent->GetGravityZ();
			}
		}
	}

	Force *= RandomizedForceScale;
	Force = ComponentTransform.InverseTransformVector(Force);

	BoneForceMap.Reset();
}

void FKawaiiPhysics_ExternalForce_Gravity::Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
                                       FKawaiiPhysicsChain& Chain,
                                       const FComponentSpacePoseContext& PoseContext, const FTransform& BoneTM)
{
	ApplyImpl(Bone, Node.DeltaTime, BoneTM);
}

void FKawaiiPhysics_ExternalForce_Gravity::ApplyMerge(FKawaiiPhysicsModifyBone& Bone, float InDeltaTime,
                                       const FTransform& BoneTM)
{
	ApplyImpl(Bone, InDeltaTime, BoneTM);
}

void FKawaiiPhysics_ExternalForce_Gravity::ApplyImpl(FKawaiiPhysicsModifyBone& Bone, float InDeltaTime,
                                       const FTransform& BoneTM)
{
	if (!CanApply(Bone))
	{
		return;
	}

	const float ForceRate = EvalForceRate(ForceRateByBoneLengthRate, Bone.LengthRateFromRoot);
	Bone.Location += 0.5f * Force * ForceRate * InDeltaTime * InDeltaTime;
	BoneForceMap.Add(Bone.BoneRef.BoneName, Force * ForceRate);
}

///////////////////////////////////////////////////////////////////////////////
// Curve
///////////////////////////////////////////////////////////////////////////////

void FKawaiiPhysics_ExternalForce_Curve::InitMaxCurveTime()
{
	// As upstream: a running maximum over each axis' last key. It is never
	// reset, so it only grows.
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const FRichCurve& Curve = ForceCurve.VectorCurves[Axis];
		if (Curve.GetNumKeys() > 0)
		{
			MaxCurveTime = FMath::Max(MaxCurveTime, Curve.GetLastKey().Time);
		}
	}
}

void FKawaiiPhysics_ExternalForce_Curve::Initialize(const FAnimationInitializeContext& Context)
{
	FKawaiiPhysics_ExternalForce::Initialize(Context);
	InitMaxCurveTime();
}

void FKawaiiPhysics_ExternalForce_Curve::PreApply(FAnimNode_KawaiiPhysics& Node, FKawaiiPhysicsChain& Chain,
                                                  const USkeletalMeshComponent* SkelComp)
{
	// The game's PreApply does not compute MaxCurveTime; only Initialize does
	// (and on the main node it runs on copies, so the live value stays as
	// loaded -- see FAnimNode_KawaiiPhysics::Initialize_AnyThread).
	FKawaiiPhysics_ExternalForce::PreApply(Node, Chain, SkelComp);
	PrevTime = Time;

	auto Wrap = [this]()
	{
		if (MaxCurveTime > 0 && Time > MaxCurveTime)
		{
			Time = FMath::Fmod(Time, MaxCurveTime);
		}
	};

	if (CurveEvaluateType == EExternalForceCurveEvaluateType::Single)
	{
		Time += Node.DeltaTime * TimeScale;
		Wrap();
		Force = ForceCurve.GetValue(Time) * RandomizedForceScale;
	}
	else
	{
		TArray<FVector> CurveValues;
		const float SubStep = Node.DeltaTime * TimeScale / SubstepCount;
		for (int32 i = 0; i < SubstepCount; ++i)
		{
			Time += SubStep;
			Wrap();
			CurveValues.Add(ForceCurve.GetValue(Time));
		}

		// Max / Min are per component, not by magnitude.
		Force = FVector::ZeroVector;
		switch (CurveEvaluateType)
		{
		case EExternalForceCurveEvaluateType::Average:
			for (const FVector& CurveValue : CurveValues)
			{
				Force += CurveValue;
			}
			Force /= static_cast<float>(SubstepCount);
			break;

		case EExternalForceCurveEvaluateType::Max:
			// Upstream seeds with FLT_MIN (the smallest POSITIVE float), so an
			// all-negative component comes out as ~0. Reproduced as is.
			Force = FVector(FLT_MIN);
			for (const FVector& CurveValue : CurveValues)
			{
				Force = FVector::Max(Force, CurveValue);
			}
			break;

		case EExternalForceCurveEvaluateType::Min:
			Force = FVector(FLT_MAX);
			for (const FVector& CurveValue : CurveValues)
			{
				Force = FVector::Min(Force, CurveValue);
			}
			break;

		default:
			break;
		}

		Force *= RandomizedForceScale;
	}

	if (ExternalForceSpace == EExternalForceSpace::WorldSpace)
	{
		Force = ComponentTransform.InverseTransformVector(Force);
	}

	BoneForceMap.Reset();
}

void FKawaiiPhysics_ExternalForce_Curve::Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
                                       FKawaiiPhysicsChain& Chain,
                                       const FComponentSpacePoseContext& PoseContext, const FTransform& BoneTM)
{
	ApplyImpl(Bone, Node.DeltaTime, BoneTM);
}

void FKawaiiPhysics_ExternalForce_Curve::ApplyMerge(FKawaiiPhysicsModifyBone& Bone, float InDeltaTime,
                                       const FTransform& BoneTM)
{
	ApplyImpl(Bone, InDeltaTime, BoneTM);
}

void FKawaiiPhysics_ExternalForce_Curve::ApplyImpl(FKawaiiPhysicsModifyBone& Bone, float InDeltaTime,
                                       const FTransform& BoneTM)
{
	if (!CanApply(Bone))
	{
		return;
	}

	ApplyLinearForce(Force, ExternalForceSpace, EvalForceRate(ForceRateByBoneLengthRate, Bone.LengthRateFromRoot),
	                 Bone, BoneTM, InDeltaTime, BoneForceMap);
}

///////////////////////////////////////////////////////////////////////////////
// Wind
///////////////////////////////////////////////////////////////////////////////

void FKawaiiPhysics_ExternalForce_Wind::PreApply(FAnimNode_KawaiiPhysics& Node, FKawaiiPhysicsChain& Chain,
                                                 const USkeletalMeshComponent* SkelComp)
{
	FKawaiiPhysics_ExternalForce::PreApply(Node, Chain, SkelComp);
	World = SkelComp ? SkelComp->GetWorld() : nullptr;
	BoneForceMap.Reset();
}

void FKawaiiPhysics_ExternalForce_Wind::Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
                                       FKawaiiPhysicsChain& Chain,
                                       const FComponentSpacePoseContext& PoseContext, const FTransform& BoneTM)
{
	ApplyImpl(Bone, Node.DeltaTime, BoneTM);
}

void FKawaiiPhysics_ExternalForce_Wind::ApplyMerge(FKawaiiPhysicsModifyBone& Bone, float InDeltaTime,
                                       const FTransform& BoneTM)
{
	ApplyImpl(Bone, InDeltaTime, BoneTM);
}

void FKawaiiPhysics_ExternalForce_Wind::ApplyImpl(FKawaiiPhysicsModifyBone& Bone, float InDeltaTime,
                                       const FTransform& BoneTM)
{
	const FSceneInterface* Scene = World && World->Scene ? World->Scene : nullptr;
	if (!CanApply(Bone) || !Scene)
	{
		return;
	}

	const float ForceRate = EvalForceRate(ForceRateByBoneLengthRate, Bone.LengthRateFromRoot);

	// Sampled at the bone's POSE location. This preset does not use the
	// chain's WindScale -- that belongs to the node-level wind only.
	FVector WindDirection = FVector::ZeroVector;
	float WindSpeed = 0.0f;
	float WindMinGust = 0.0f;
	float WindMaxGust = 0.0f;
	Scene->GetWindParameters(ComponentTransform.TransformPosition(Bone.PoseLocation), WindDirection,
	                         WindSpeed, WindMinGust, WindMaxGust);

	WindDirection = ComponentTransform.InverseTransformVector(WindDirection);
	WindDirection *= WindSpeed;

	Bone.Location += WindDirection * ForceRate * RandomizedForceScale * InDeltaTime;
	BoneForceMap.Add(Bone.BoneRef.BoneName, WindDirection * ForceRate * RandomizedForceScale);
}
