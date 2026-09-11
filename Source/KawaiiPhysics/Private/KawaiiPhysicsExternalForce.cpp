// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License

#include "KawaiiPhysicsExternalForce.h"

#include "Animation/AnimInstanceProxy.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "SceneInterface.h"

namespace
{
	/** Samples ForceRateByBoneLengthRate, leaving the force alone if unauthored. */
	float EvalForceRate(const FRuntimeFloatCurve& CurveData, float LengthRate)
	{
		const FRichCurve* Curve = CurveData.GetRichCurveConst();
		return (Curve && Curve->GetNumKeys() > 0) ? Curve->Eval(LengthRate) : 1.0f;
	}

	/** Moves a component-space force into whichever space the author picked. */
	FVector ConvertForceToComponentSpace(const FVector& ForceWS, EExternalForceSpace Space,
	                                     const FTransform& ComponentTransform, const FTransform& BoneTM)
	{
		switch (Space)
		{
		case EExternalForceSpace::WorldSpace:
			return ComponentTransform.InverseTransformVector(ForceWS);
		case EExternalForceSpace::BoneSpace:
			return BoneTM.TransformVector(ForceWS);
		case EExternalForceSpace::ComponentSpace:
		default:
			return ForceWS;
		}
	}
}

///////////////////////////////////////////////////////////////////////////////
// Basic
///////////////////////////////////////////////////////////////////////////////

void FKawaiiPhysics_ExternalForce_Basic::PreApply(FAnimNode_KawaiiPhysics& Node, FKawaiiPhysicsChain& Chain,
                                                  const USkeletalMeshComponent* SkelComp)
{
	FKawaiiPhysics_ExternalForce::PreApply(Node, Chain, SkelComp);

	Force = FVector::Zero();

	if (Interval <= 0.0f)
	{
		// Continuous.
		Force = ForceDir * RandomizedForceScale;
	}
	else
	{
		Time += Node.DeltaTime;
		if (Time - PrevTime >= Interval)
		{
			PrevTime = Time;
			Force = ForceDir * RandomizedForceScale;
		}
	}

#if ENABLE_ANIM_DEBUG
	BoneForceMap.Reset();
#endif
}

void FKawaiiPhysics_ExternalForce_Basic::Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
                                               FKawaiiPhysicsChain& Chain,
                                               const FComponentSpacePoseContext& PoseContext, const FTransform& BoneTM)
{
	if (Force.IsZero() || !CanApply(Bone))
	{
		return;
	}

	const FVector BoneForce = ConvertForceToComponentSpace(Force, ExternalForceSpace, ComponentTransform, BoneTM)
		* EvalForceRate(ForceRateByBoneLengthRate, Bone.LengthRateFromRoot);

	Bone.Location += BoneForce * Node.DeltaTime * Node.DeltaTime;

#if ENABLE_ANIM_DEBUG
	BoneForceMap.Add(Bone.BoneRef.BoneName, BoneForce);
#endif
}

///////////////////////////////////////////////////////////////////////////////
// Gravity
///////////////////////////////////////////////////////////////////////////////

void FKawaiiPhysics_ExternalForce_Gravity::PreApply(FAnimNode_KawaiiPhysics& Node, FKawaiiPhysicsChain& Chain,
                                                    const USkeletalMeshComponent* SkelComp)
{
	FKawaiiPhysics_ExternalForce::PreApply(Node, Chain, SkelComp);

	FVector GravityDirection = FVector(0.0f, 0.0f, -1.0f);
	float GravityScale = 1.0f;

	if (bUseOverrideGravityDirection && !OverrideGravityDirection.IsNearlyZero())
	{
		GravityDirection = OverrideGravityDirection.GetSafeNormal();
	}

	if (const ACharacter* Character = SkelComp ? Cast<ACharacter>(SkelComp->GetOwner()) : nullptr)
	{
		if (const UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			if (bUseCharacterGravityScale)
			{
				GravityScale = Movement->GravityScale;
			}

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 4
			if (bUseCharacterGravityDirection)
			{
				GravityDirection = Movement->GetGravityDirection();
			}
#else
			// Custom gravity direction only exists from 5.4; on 5.3 the movement
			// component is always -Z, so there is nothing to read.
#endif
		}
	}

	const float WorldGravityZ = SkelComp && SkelComp->GetWorld()
		                            ? SkelComp->GetWorld()->GetGravityZ()
		                            : -980.0f;

	Force = GravityDirection * FMath::Abs(WorldGravityZ) * GravityScale * RandomizedForceScale;

#if ENABLE_ANIM_DEBUG
	BoneForceMap.Reset();
#endif
}

void FKawaiiPhysics_ExternalForce_Gravity::Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
                                                 FKawaiiPhysicsChain& Chain,
                                                 const FComponentSpacePoseContext& PoseContext,
                                                 const FTransform& BoneTM)
{
	if (Force.IsZero() || !CanApply(Bone))
	{
		return;
	}

	// Gravity is always world space, and is additionally scaled per bone by the
	// chain's own GravityScale so a single chain can be made lighter or heavier.
	const FVector BoneForce = ComponentTransform.InverseTransformVector(Force)
		* EvalForceRate(ForceRateByBoneLengthRate, Bone.LengthRateFromRoot)
		* Bone.PhysicsSettings.GravityScale;

	Bone.Location += BoneForce * Node.DeltaTime * Node.DeltaTime;

#if ENABLE_ANIM_DEBUG
	BoneForceMap.Add(Bone.BoneRef.BoneName, BoneForce);
#endif
}

///////////////////////////////////////////////////////////////////////////////
// Curve
///////////////////////////////////////////////////////////////////////////////

void FKawaiiPhysics_ExternalForce_Curve::InitMaxCurveTime()
{
	MaxCurveTime = 0.0f;

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
	Time = 0.0f;
	PrevTime = 0.0f;
}

void FKawaiiPhysics_ExternalForce_Curve::PreApply(FAnimNode_KawaiiPhysics& Node, FKawaiiPhysicsChain& Chain,
                                                  const USkeletalMeshComponent* SkelComp)
{
	FKawaiiPhysics_ExternalForce::PreApply(Node, Chain, SkelComp);

	if (MaxCurveTime <= 0.0f)
	{
		InitMaxCurveTime();
		if (MaxCurveTime <= 0.0f)
		{
			Force = FVector::Zero();
			return;
		}
	}

	PrevTime = Time;
	Time = FMath::Fmod(Time + Node.DeltaTime * TimeScale, MaxCurveTime);

	auto Sample = [this](float SampleTime)
	{
		return FVector(
			ForceCurve.VectorCurves[0].Eval(SampleTime),
			ForceCurve.VectorCurves[1].Eval(SampleTime),
			ForceCurve.VectorCurves[2].Eval(SampleTime));
	};

	if (CurveEvaluateType == EExternalForceCurveEvaluateType::Single || SubstepCount <= 1)
	{
		Force = Sample(Time);
	}
	else
	{
		// Subdivide the elapsed interval so a fast-moving curve is not aliased
		// down to whatever single value happened to land on this frame.
		const float Step = Node.DeltaTime * TimeScale / static_cast<float>(SubstepCount);

		FVector Accumulated = FVector::Zero();
		FVector Best = Sample(PrevTime);

		for (int32 Index = 0; Index < SubstepCount; ++Index)
		{
			const float SampleTime = FMath::Fmod(PrevTime + Step * (Index + 1), MaxCurveTime);
			const FVector Sampled = Sample(SampleTime);

			switch (CurveEvaluateType)
			{
			case EExternalForceCurveEvaluateType::Average:
				Accumulated += Sampled;
				break;
			case EExternalForceCurveEvaluateType::Max:
				if (Sampled.SizeSquared() > Best.SizeSquared()) Best = Sampled;
				break;
			case EExternalForceCurveEvaluateType::Min:
				if (Sampled.SizeSquared() < Best.SizeSquared()) Best = Sampled;
				break;
			default:
				break;
			}
		}

		Force = CurveEvaluateType == EExternalForceCurveEvaluateType::Average
			        ? Accumulated / static_cast<float>(SubstepCount)
			        : Best;
	}

	Force *= RandomizedForceScale;

#if ENABLE_ANIM_DEBUG
	BoneForceMap.Reset();
#endif
}

void FKawaiiPhysics_ExternalForce_Curve::Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
                                               FKawaiiPhysicsChain& Chain,
                                               const FComponentSpacePoseContext& PoseContext, const FTransform& BoneTM)
{
	if (Force.IsZero() || !CanApply(Bone))
	{
		return;
	}

	const FVector BoneForce = ConvertForceToComponentSpace(Force, ExternalForceSpace, ComponentTransform, BoneTM)
		* EvalForceRate(ForceRateByBoneLengthRate, Bone.LengthRateFromRoot);

	Bone.Location += BoneForce * Node.DeltaTime * Node.DeltaTime;

#if ENABLE_ANIM_DEBUG
	BoneForceMap.Add(Bone.BoneRef.BoneName, BoneForce);
#endif
}

///////////////////////////////////////////////////////////////////////////////
// Wind
///////////////////////////////////////////////////////////////////////////////

void FKawaiiPhysics_ExternalForce_Wind::PreApply(FAnimNode_KawaiiPhysics& Node, FKawaiiPhysicsChain& Chain,
                                                 const USkeletalMeshComponent* SkelComp)
{
	FKawaiiPhysics_ExternalForce::PreApply(Node, Chain, SkelComp);

	World = SkelComp ? SkelComp->GetWorld() : nullptr;

#if ENABLE_ANIM_DEBUG
	BoneForceMap.Reset();
#endif
}

void FKawaiiPhysics_ExternalForce_Wind::Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
                                              FKawaiiPhysicsChain& Chain,
                                              const FComponentSpacePoseContext& PoseContext, const FTransform& BoneTM)
{
	if (!World || !World->Scene || !CanApply(Bone))
	{
		return;
	}

	// Wind is sampled per bone, since directional sources fall off with distance.
	const FVector WorldLocation = ComponentTransform.TransformPosition(Bone.Location);

	FVector WindDirection = FVector::ZeroVector;
	float WindSpeed = 0.0f;
	float WindMinGust = 0.0f;
	float WindMaxGust = 0.0f;
	World->Scene->GetWindParameters_GameThread(WorldLocation, WindDirection, WindSpeed, WindMinGust, WindMaxGust);

	Force = WindDirection * WindSpeed * RandomizedForceScale;

	const FVector BoneForce = ComponentTransform.InverseTransformVector(Force)
		* EvalForceRate(ForceRateByBoneLengthRate, Bone.LengthRateFromRoot)
		* Chain.ExternalForceSettings.WindScale;

	Bone.Location += BoneForce * Node.DeltaTime * Node.DeltaTime;

#if ENABLE_ANIM_DEBUG
	BoneForceMap.Add(Bone.BoneRef.BoneName, BoneForce);
#endif
}
