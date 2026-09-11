// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License
//
// Limit resolution, collision response and the constraint solvers.

#include "AnimNode_KawaiiPhysics.h"

#include "KawaiiPhysics.h"
#include "KawaiiPhysicsLimitsDataAsset.h"
#include "KawaiiPhysicsBoneConstraintsDataAsset.h"
#include "KawaiiPhysicsSolvers.h"

#include "Animation/AnimInstanceProxy.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
// USkeletalBodySetup lives in PhysicsAsset.h in 5.3; there is no separate header.
#include "PhysicsEngine/PhysicsAsset.h"

///////////////////////////////////////////////////////////////////////////////
// Limit sources
///////////////////////////////////////////////////////////////////////////////

void FAnimNode_KawaiiPhysics::ApplyLimitsDataAsset(const FBoneContainer& RequiredBones)
{
	SphericalLimitsData.Empty();
	CapsuleLimitsData.Empty();
	BoxLimitsData.Empty();
	PlanarLimitsData.Empty();

	if (!LimitsDataAsset)
	{
		return;
	}

	SphericalLimitsData = LimitsDataAsset->SphericalLimits;
	CapsuleLimitsData = LimitsDataAsset->CapsuleLimits;
	BoxLimitsData = LimitsDataAsset->BoxLimits;
	PlanarLimitsData = LimitsDataAsset->PlanarLimits;

	auto Tag = [](auto& Limits)
	{
		for (auto& Limit : Limits)
		{
			Limit.SourceType = ECollisionSourceType::DataAsset;
		}
	};

	Tag(SphericalLimitsData);
	Tag(CapsuleLimitsData);
	Tag(BoxLimitsData);
	Tag(PlanarLimitsData);
}

void FAnimNode_KawaiiPhysics::ApplyPhysicsAsset(const FBoneContainer& RequiredBones)
{
	if (!PhysicsAssetForLimits)
	{
		return;
	}

	// Appended to whatever the data asset already contributed, so a node can
	// draw limits from both sources at once.
	for (const USkeletalBodySetup* BodySetup : PhysicsAssetForLimits->SkeletalBodySetups)
	{
		if (!BodySetup)
		{
			continue;
		}

		const FName BoneName = BodySetup->BoneName;

		for (const FKSphereElem& Sphere : BodySetup->AggGeom.SphereElems)
		{
			FSphericalLimit Limit;
			Limit.DrivingBone.BoneName = BoneName;
			Limit.DrivingBone.Initialize(RequiredBones);
			Limit.OffsetLocation = Sphere.Center;
			Limit.Radius = Sphere.Radius;
			Limit.LimitType = ESphericalLimitType::Outer;
			Limit.SourceType = ECollisionSourceType::PhysicsAsset;
			SphericalLimitsData.Add(Limit);
		}

		for (const FKSphylElem& Sphyl : BodySetup->AggGeom.SphylElems)
		{
			FCapsuleLimit Limit;
			Limit.DrivingBone.BoneName = BoneName;
			Limit.DrivingBone.Initialize(RequiredBones);
			Limit.OffsetLocation = Sphyl.Center;
			Limit.OffsetRotation = Sphyl.Rotation;
			Limit.Radius = Sphyl.Radius;
			Limit.Length = Sphyl.Length;
			Limit.SourceType = ECollisionSourceType::PhysicsAsset;
			CapsuleLimitsData.Add(Limit);
		}

		for (const FKBoxElem& Box : BodySetup->AggGeom.BoxElems)
		{
			FBoxLimit Limit;
			Limit.DrivingBone.BoneName = BoneName;
			Limit.DrivingBone.Initialize(RequiredBones);
			Limit.OffsetLocation = Box.Center;
			Limit.OffsetRotation = Box.Rotation;
			Limit.Extent = FVector(Box.X, Box.Y, Box.Z) * 0.5f;
			Limit.SourceType = ECollisionSourceType::PhysicsAsset;
			BoxLimitsData.Add(Limit);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////
// Limit transform resolution
///////////////////////////////////////////////////////////////////////////////

void FAnimNode_KawaiiPhysics::UpdateSphericalLimits(TArray<FSphericalLimit>& Limits,
                                                    FComponentSpacePoseContext& Output,
                                                    const FBoneContainer& BoneContainer,
                                                    const FTransform& ComponentTransform)
{
	for (FSphericalLimit& Limit : Limits)
	{
		KawaiiPhysics::ResolveLimitTransform(Limit, Output, BoneContainer, ComponentTransform, ParentCompTransform);
	}
}

void FAnimNode_KawaiiPhysics::UpdateCapsuleLimits(TArray<FCapsuleLimit>& Limits, FComponentSpacePoseContext& Output,
                                                  const FBoneContainer& BoneContainer,
                                                  const FTransform& ComponentTransform)
{
	for (FCapsuleLimit& Limit : Limits)
	{
		KawaiiPhysics::ResolveLimitTransform(Limit, Output, BoneContainer, ComponentTransform, ParentCompTransform);
	}
}

void FAnimNode_KawaiiPhysics::UpdateBoxLimits(TArray<FBoxLimit>& Limits, FComponentSpacePoseContext& Output,
                                              const FBoneContainer& BoneContainer,
                                              const FTransform& ComponentTransform)
{
	for (FBoxLimit& Limit : Limits)
	{
		KawaiiPhysics::ResolveLimitTransform(Limit, Output, BoneContainer, ComponentTransform, ParentCompTransform);
	}
}

void FAnimNode_KawaiiPhysics::UpdatePlanerLimits(TArray<FPlanarLimit>& Limits, FComponentSpacePoseContext& Output,
                                                 const FBoneContainer& BoneContainer,
                                                 const FTransform& ComponentTransform)
{
	for (FPlanarLimit& Limit : Limits)
	{
		KawaiiPhysics::ResolveLimitTransform(Limit, Output, BoneContainer, ComponentTransform, ParentCompTransform);

		// The plane travels with its driving bone: normal from the rotated up
		// axis, through the resolved location.
		Limit.Plane = FPlane(Limit.Location, Limit.Rotation.GetAxisZ());
	}
}

///////////////////////////////////////////////////////////////////////////////
// Collision response
///////////////////////////////////////////////////////////////////////////////

void FAnimNode_KawaiiPhysics::AdjustBySphereCollision(FKawaiiPhysicsModifyBone& Bone, TArray<FSphericalLimit>& Limits)
{
	KawaiiPhysics::AdjustBySphereCollision(Bone, Limits);
}

void FAnimNode_KawaiiPhysics::AdjustByCapsuleCollision(FKawaiiPhysicsModifyBone& Bone, TArray<FCapsuleLimit>& Limits)
{
	KawaiiPhysics::AdjustByCapsuleCollision(Bone, Limits);
}

void FAnimNode_KawaiiPhysics::AdjustByBoxCollision(FKawaiiPhysicsModifyBone& Bone, TArray<FBoxLimit>& Limits)
{
	KawaiiPhysics::AdjustByBoxCollision(Bone, Limits);
}

void FAnimNode_KawaiiPhysics::AdjustByPlanerCollision(FKawaiiPhysicsModifyBone& Bone, TArray<FPlanarLimit>& Limits)
{
	KawaiiPhysics::AdjustByPlanerCollision(Bone, Limits);
}

void FAnimNode_KawaiiPhysics::AdjustByWorldCollision(FKawaiiPhysicsChain& Chain, FKawaiiPhysicsModifyBone& Bone,
                                                     const USkeletalMeshComponent* OwningComp)
{
	if (!OwningComp || !OwningComp->GetWorld())
	{
		return;
	}

	const float Radius = Bone.PhysicsSettings.Radius;
	if (Radius <= 0.0f)
	{
		return;
	}

	const UWorld* World = OwningComp->GetWorld();
	const FTransform ComponentTransform = OwningComp->GetComponentTransform();
	const FVector WorldLocation = ComponentTransform.TransformPosition(Bone.Location);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(KawaiiPhysicsWorldCollision), false);

	if (bIgnoreSelfComponent)
	{
		Params.AddIgnoredComponent(OwningComp);
	}
	else
	{
		// Selective self-ignore: named bones plus anything matching a prefix.
		if (const AActor* Owner = OwningComp->GetOwner())
		{
			Params.AddIgnoredActor(Owner);
		}
	}

	FCollisionResponseParams ResponseParams;
	ECollisionChannel TraceChannel = ECC_WorldStatic;

	if (bOverrideCollisionParams)
	{
		TraceChannel = CollisionChannelSettings.GetObjectType();
		ResponseParams = FCollisionResponseParams(CollisionChannelSettings.GetResponseToChannels());
	}
	else if (OwningComp->GetBodyInstance())
	{
		TraceChannel = OwningComp->GetCollisionObjectType();
		ResponseParams = FCollisionResponseParams(OwningComp->GetCollisionResponseToChannels());
	}

	FHitResult Hit;
	const bool bBlocked = World->SweepSingleByChannel(
		Hit, WorldLocation, WorldLocation, FQuat::Identity, TraceChannel,
		FCollisionShape::MakeSphere(Radius), Params, ResponseParams);

	if (bBlocked && Hit.bStartPenetrating)
	{
		const FVector PushOutWS = Hit.Normal * (Hit.PenetrationDepth + KINDA_SMALL_NUMBER);
		Bone.Location += ComponentTransform.InverseTransformVector(PushOutWS);
	}
}

///////////////////////////////////////////////////////////////////////////////
// Positional and angular limits
///////////////////////////////////////////////////////////////////////////////

void FAnimNode_KawaiiPhysics::AdjustByAngleLimit(FKawaiiPhysicsChain& Chain, FKawaiiPhysicsModifyBone& Bone,
                                                 const FKawaiiPhysicsModifyBone& ParentBone)
{
	KawaiiPhysics::AdjustByAngleLimit(Bone, ParentBone);
}

void FAnimNode_KawaiiPhysics::AdjustByLinearLimit(FKawaiiPhysicsModifyBone& Bone)
{
	KawaiiPhysics::AdjustByLinearLimit(Bone);
}

void FAnimNode_KawaiiPhysics::AdjustByPlanarConstraint(FKawaiiPhysicsChain& Chain, FKawaiiPhysicsModifyBone& Bone,
                                                       const FKawaiiPhysicsModifyBone& ParentBone)
{
	KawaiiPhysics::AdjustByPlanarConstraint(Bone, Chain.PhysicsSettings.PlanarConstraint);
}

void FAnimNode_KawaiiPhysics::AdjustByRelativeRotationLimit(FKawaiiPhysicsChain& Chain,
                                                            FKawaiiPhysicsModifyBone& Bone,
                                                            const FKawaiiPhysicsModifyBone& ParentBone)
{
	const FBoneChainPhysicsSettings& Settings = Chain.PhysicsSettings;
	if (Settings.bUseCurve || !Settings.PhysicsParamsPerBone.IsValidIndex(Bone.Index))
	{
		return;
	}

	const FBonePhysicsParams& Params = Settings.PhysicsParamsPerBone[Bone.Index];
	if (!Params.bLimitRelativeRotation)
	{
		return;
	}

	// Work in the parent's frame, where the constraint is authored: the bone may
	// only rotate away from its pose along the half-axes that are allowed.
	const FQuat ParentInverse = ParentBone.Rotation.Inverse();
	const FQuat PoseRelative = ParentBone.PoseRotation.Inverse() * Bone.PoseRotation;
	const FQuat SimRelative = ParentInverse * Bone.Rotation;

	const FQuat Delta = PoseRelative.Inverse() * SimRelative;
	FRotator DeltaRotator = Delta.Rotator();

	auto ClampAxis = [](double Value, bool bAllowPositive, bool bAllowNegative)
	{
		if (Value > 0.0 && !bAllowPositive) return 0.0;
		if (Value < 0.0 && !bAllowNegative) return 0.0;
		return Value;
	};

	DeltaRotator.Roll = ClampAxis(DeltaRotator.Roll,
	                              Params.bAllowPositiveRelativeRotationX, Params.bAllowNegativeRelativeRotationX);
	DeltaRotator.Pitch = ClampAxis(DeltaRotator.Pitch,
	                               Params.bAllowPositiveRelativeRotationY, Params.bAllowNegativeRelativeRotationY);
	DeltaRotator.Yaw = ClampAxis(DeltaRotator.Yaw,
	                             Params.bAllowPositiveRelativeRotationZ, Params.bAllowNegativeRelativeRotationZ);

	Bone.Rotation = ParentBone.Rotation * PoseRelative * DeltaRotator.Quaternion();
}

///////////////////////////////////////////////////////////////////////////////
// Bone constraints (XPBD)
///////////////////////////////////////////////////////////////////////////////

void FAnimNode_KawaiiPhysics::ApplyBoneConstraintDataAsset(FKawaiiPhysicsChain& Chain,
                                                           const FBoneContainer& RequiredBones)
{
	FBoneConstraintSettings& Settings = Chain.BoneConstraintSettings;
	Settings.BoneConstraintsData.Empty();

	if (!Settings.BoneConstraintsDataAsset)
	{
		return;
	}

	Settings.BoneConstraintsData = Settings.BoneConstraintsDataAsset->GenerateBoneConstraints();

	for (FModifyBoneConstraint& Constraint : Settings.BoneConstraintsData)
	{
		Constraint.InitializeBone(RequiredBones);
	}
}

void FAnimNode_KawaiiPhysics::InitBoneConstraints(FKawaiiPhysicsChain& Chain)
{
	FBoneConstraintSettings& Settings = Chain.BoneConstraintSettings;

	Settings.MergedBoneConstraints.Reset();
	Settings.MergedBoneConstraints.Append(Settings.BoneConstraints);
	Settings.MergedBoneConstraints.Append(Settings.BoneConstraintsData);

	// Resolve bone names to positions in this chain, and capture rest lengths.
	auto FindModifyBone = [&Chain](const FName BoneName)
	{
		return Chain.ModifyBones.IndexOfByPredicate(
			[BoneName](const FKawaiiPhysicsModifyBone& Bone)
			{
				return !Bone.bDummy && Bone.BoneRef.BoneName == BoneName;
			});
	};

	for (FModifyBoneConstraint& Constraint : Settings.MergedBoneConstraints)
	{
		Constraint.ModifyBoneIndex1 = FindModifyBone(Constraint.Bone1.BoneName);
		Constraint.ModifyBoneIndex2 = FindModifyBone(Constraint.Bone2.BoneName);
		Constraint.Lambda = 0.0f;

		if (!Constraint.IsBoneReferenceValid())
		{
			Constraint.Length = -1.0f;
			continue;
		}

		Constraint.Length = (Chain.ModifyBones[Constraint.ModifyBoneIndex1].PoseLocation -
			Chain.ModifyBones[Constraint.ModifyBoneIndex2].PoseLocation).Size();
	}

	if (!Settings.bAutoAddChildDummyBoneConstraint)
	{
		return;
	}

	// Where two constrained bones each own a dummy tail, constrain the tails too,
	// otherwise the very ends of a skirt splay apart.
	const int32 AuthoredCount = Settings.MergedBoneConstraints.Num();
	for (int32 Index = 0; Index < AuthoredCount; ++Index)
	{
		const FModifyBoneConstraint& Source = Settings.MergedBoneConstraints[Index];
		if (!Source.IsBoneReferenceValid() || !Source.IsValid())
		{
			continue;
		}

		// Explicit return type: INDEX_NONE is an unnamed enum, so without it the
		// two returns deduce to different types.
		auto FindDummyChild = [&Chain](int32 BoneIndex) -> int32
		{
			for (const int32 ChildIndex : Chain.ModifyBones[BoneIndex].ChildIndices)
			{
				if (Chain.ModifyBones[ChildIndex].bDummy)
				{
					return ChildIndex;
				}
			}
			return INDEX_NONE;
		};

		const int32 Dummy1 = FindDummyChild(Source.ModifyBoneIndex1);
		const int32 Dummy2 = FindDummyChild(Source.ModifyBoneIndex2);

		if (Dummy1 == INDEX_NONE || Dummy2 == INDEX_NONE)
		{
			continue;
		}

		FModifyBoneConstraint DummyConstraint;
		DummyConstraint.bIsDummy = true;
		DummyConstraint.bOverrideCompliance = Source.bOverrideCompliance;
		DummyConstraint.ComplianceType = Source.ComplianceType;
		DummyConstraint.ModifyBoneIndex1 = Dummy1;
		DummyConstraint.ModifyBoneIndex2 = Dummy2;
		DummyConstraint.Length = (Chain.ModifyBones[Dummy1].PoseLocation -
			Chain.ModifyBones[Dummy2].PoseLocation).Size();

		Settings.MergedBoneConstraints.Add(DummyConstraint);
	}
}

void FAnimNode_KawaiiPhysics::AdjustByBoneConstraints(FKawaiiPhysicsChain& Chain, int32 IterationCount)
{
	FBoneConstraintSettings& Settings = Chain.BoneConstraintSettings;

	if (IterationCount <= 0 || Settings.MergedBoneConstraints.IsEmpty() || DeltaTime <= 0.0f)
	{
		return;
	}

	// XPBD: alpha-tilde is compliance over dt squared, and lambda accumulates
	// across the iterations of a single frame.
	const float DeltaTimeSq = DeltaTime * DeltaTime;

	for (FModifyBoneConstraint& Constraint : Settings.MergedBoneConstraints)
	{
		Constraint.Lambda = 0.0f;
	}

	for (int32 Iteration = 0; Iteration < IterationCount; ++Iteration)
	{
		for (FModifyBoneConstraint& Constraint : Settings.MergedBoneConstraints)
		{
			if (!Constraint.IsBoneReferenceValid() || !Constraint.IsValid())
			{
				continue;
			}

			if (!Chain.ModifyBones.IsValidIndex(Constraint.ModifyBoneIndex1) ||
				!Chain.ModifyBones.IsValidIndex(Constraint.ModifyBoneIndex2))
			{
				continue;
			}

			FKawaiiPhysicsModifyBone& Bone1 = Chain.ModifyBones[Constraint.ModifyBoneIndex1];
			FKawaiiPhysicsModifyBone& Bone2 = Chain.ModifyBones[Constraint.ModifyBoneIndex2];

			const EXPBDComplianceType Compliance = Constraint.bOverrideCompliance
				                                       ? Constraint.ComplianceType
				                                       : Settings.BoneConstraintGlobalComplianceType;
			const float AlphaTilde = KawaiiPhysics::GetComplianceValue(Compliance) / DeltaTimeSq;

			KawaiiPhysics::SolveDistanceConstraint(Bone1, Bone2, Constraint.Length, AlphaTilde, Constraint.Lambda);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////
// Debug
///////////////////////////////////////////////////////////////////////////////

#if ENABLE_ANIM_DEBUG
void FAnimNode_KawaiiPhysics::AnimDrawDebug(const FComponentSpacePoseContext& Output)
{
	if (!CVarAnimNodeKawaiiPhysicsDebug.GetValueOnAnyThread())
	{
		return;
	}

	FAnimInstanceProxy* Proxy = Output.AnimInstanceProxy;
	const FTransform ComponentTransform = Proxy->GetComponentTransform();
	const bool bShowLengthRate = CVarAnimNodeKawaiiPhysicsDebugLengthRate.GetValueOnAnyThread();

	for (const FKawaiiPhysicsChain& Chain : Chains)
	{
		for (const FKawaiiPhysicsModifyBone& Bone : Chain.ModifyBones)
		{
			const FVector LocationWS = ComponentTransform.TransformPosition(Bone.Location);

			const FColor Colour = Bone.bDummy
				                      ? FColor::Yellow
				                      : (Bone.IsSimulated() ? FColor::White : FColor::Red);

			Proxy->AnimDrawDebugSphere(LocationWS, Bone.PhysicsSettings.Radius, 8, Colour, false, -1.0f, 0.3f);

			if (Bone.HasParent())
			{
				const FVector ParentWS =
					ComponentTransform.TransformPosition(Chain.ModifyBones[Bone.ParentIndex].Location);
				Proxy->AnimDrawDebugLine(ParentWS, LocationWS, FColor::Green, false, -1.0f, 0.3f);
			}

			if (bShowLengthRate)
			{
				Proxy->AnimDrawDebugOnScreenMessage(
					FString::Printf(TEXT("%s: %.3f"), *Bone.BoneRef.BoneName.ToString(), Bone.LengthRateFromRoot),
					FColor::White);
			}
		}
	}
}
#endif
