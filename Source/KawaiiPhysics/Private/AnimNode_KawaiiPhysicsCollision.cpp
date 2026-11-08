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
	BuildPhysicsAssetLimits(PhysicsAssetForLimits, RequiredBones, SphericalLimitsData, CapsuleLimitsData,
	                        BoxLimitsData);
}

void FAnimNode_KawaiiPhysics::BuildPhysicsAssetLimits(const UPhysicsAsset* PhysicsAsset,
                                                      const FBoneContainer& RequiredBones,
                                                      TArray<FSphericalLimit>& SphericalLimitsData,
                                                      TArray<FCapsuleLimit>& CapsuleLimitsData,
                                                      TArray<FBoxLimit>& BoxLimitsData)
{
	// Game 0x7671350 (main) / 0x7671BE0 (Merge): replace the physics-asset
	// limits; a body whose bone is not in this pose is skipped entirely.
	auto RemovePA = [](auto& Limits)
	{
		Limits.RemoveAll([](const auto& Limit) { return Limit.SourceType == ECollisionSourceType::PhysicsAsset; });
	};
	RemovePA(SphericalLimitsData);
	RemovePA(CapsuleLimitsData);
	RemovePA(BoxLimitsData);

	if (!PhysicsAsset)
	{
		return;
	}

	for (const USkeletalBodySetup* BodySetup : PhysicsAsset->SkeletalBodySetups)
	{
		if (!BodySetup)
		{
			continue;
		}

		const FName BoneName = BodySetup->BoneName;
		FBoneReference BodyBone(BoneName);
		BodyBone.Initialize(RequiredBones);
		if (!BodyBone.IsValidToEvaluate(RequiredBones))
		{
			continue;
		}

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
		KawaiiPhysics::ResolveLimitTransform(Limit, Output, BoneContainer, true);
	}
}

void FAnimNode_KawaiiPhysics::UpdateCapsuleLimits(TArray<FCapsuleLimit>& Limits, FComponentSpacePoseContext& Output,
                                                  const FBoneContainer& BoneContainer,
                                                  const FTransform& ComponentTransform)
{
	for (FCapsuleLimit& Limit : Limits)
	{
		KawaiiPhysics::ResolveLimitTransform(Limit, Output, BoneContainer, true);
	}
}

void FAnimNode_KawaiiPhysics::UpdateBoxLimits(TArray<FBoxLimit>& Limits, FComponentSpacePoseContext& Output,
                                              const FBoneContainer& BoneContainer,
                                              const FTransform& ComponentTransform)
{
	for (FBoxLimit& Limit : Limits)
	{
		KawaiiPhysics::ResolveLimitTransform(Limit, Output, BoneContainer, true);
	}
}

void FAnimNode_KawaiiPhysics::UpdatePlanerLimits(TArray<FPlanarLimit>& Limits, FComponentSpacePoseContext& Output,
                                                 const FBoneContainer& BoneContainer,
                                                 const FTransform& ComponentTransform)
{
	for (FPlanarLimit& Limit : Limits)
	{
		KawaiiPhysics::ResolvePlanarLimitTransform(Limit, Output, BoneContainer);
	}
}

///////////////////////////////////////////////////////////////////////////////
// Collision response
///////////////////////////////////////////////////////////////////////////////

void FAnimNode_KawaiiPhysics::AdjustBySphereCollision(FKawaiiPhysicsModifyBone& Bone, TArray<FSphericalLimit>& Limits)
{
	KawaiiPhysics::AdjustBySphereCollision(Bone, Limits, true);
}

void FAnimNode_KawaiiPhysics::AdjustByCapsuleCollision(FKawaiiPhysicsModifyBone& Bone, TArray<FCapsuleLimit>& Limits)
{
	// Game 0x766A990: a bone with a capsule CustomShape (half height >= 1e-4)
	// collides as a segment along its shape rotation's Z axis, pushed out
	// rigidly along the closest-point separation; otherwise as a point. The
	// capsule's radius and length are scaled by its driving bone's scale.
	if (Bone.ShapeHalfHeight < 1e-4f)
	{
		KawaiiPhysics::AdjustByCapsuleCollision(Bone, Limits, true);
		return;
	}

	for (const FCapsuleLimit& Capsule : Limits)
	{
		if (!Capsule.bEnable || Capsule.Radius <= 0 || Capsule.Length <= 0)
		{
			continue;
		}

		const float Scale = Capsule.DrivingBoneScale;   // main node only (0x766AA6D)
		const FVector Axis = Capsule.Rotation.GetAxisZ() * Capsule.Length * Scale * 0.5f;
		const FVector BoneAxis = Bone.ShapeRotation.GetAxisZ() * Bone.ShapeHalfHeight;

		FVector OnBone, OnCapsule;
		FMath::SegmentDistToSegmentSafe(Bone.Location + BoneAxis, Bone.Location - BoneAxis,
		                                Capsule.Location + Axis, Capsule.Location - Axis, OnBone, OnCapsule);

		const FVector Separation = OnBone - OnCapsule;
		const float Distance = Separation.Size();
		const float LimitDistance = Bone.PhysicsSettings.Radius + Capsule.Radius * Scale;
		if (Distance < LimitDistance)
		{
			Bone.Location += Separation.GetSafeNormal() * (LimitDistance - Distance);
		}
	}
}

void FAnimNode_KawaiiPhysics::AdjustByBoxCollision(FKawaiiPhysicsModifyBone& Bone, TArray<FBoxLimit>& Limits)
{
	KawaiiPhysics::AdjustByBoxCollision(Bone, Limits, true);
}

void FAnimNode_KawaiiPhysics::AdjustByPlanerCollision(FKawaiiPhysicsModifyBone& Bone, TArray<FPlanarLimit>& Limits)
{
	KawaiiPhysics::AdjustByPlanerCollision(Bone, Limits);
}

void FAnimNode_KawaiiPhysics::AdjustByWorldCollision(FKawaiiPhysicsChain& Chain, FKawaiiPhysicsModifyBone& Bone,
                                                     const USkeletalMeshComponent* OwningComp)
{
	// As upstream v1.18.0: a sphere SWEEP from last frame's position to this
	// one -- so a fast bone cannot tunnel through -- resolving to the hit point,
	// or out along the normal if it started inside.
	if (!OwningComp || !Bone.HasParent())
	{
		return;
	}

	const UWorld* World = OwningComp->GetWorld();
	if (!World)
	{
		return;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(KawaiiCollision));
	if (bIgnoreSelfComponent)
	{
		Params.AddIgnoredComponent(OwningComp);
	}

	const ECollisionChannel TraceChannel = bOverrideCollisionParams
		                                       ? CollisionChannelSettings.GetObjectType()
		                                       : OwningComp->GetCollisionObjectType();
	const FCollisionResponseParams ResponseParams = bOverrideCollisionParams
		                                                ? FCollisionResponseParams(
			                                                CollisionChannelSettings.GetResponseToChannels())
		                                                : FCollisionResponseParams(
			                                                OwningComp->GetCollisionResponseToChannels());

	const FTransform CompTransform = OwningComp->GetComponentTransform();
	const FVector Start = CompTransform.TransformPosition(Bone.PrevLocation);
	const FVector End = CompTransform.TransformPosition(Bone.Location);
	const FCollisionShape Shape = FCollisionShape::MakeSphere(Bone.PhysicsSettings.Radius);

	auto Resolve = [&](const FHitResult& Hit)
	{
		if (Hit.bStartPenetrating)
		{
			Bone.Location = CompTransform.InverseTransformPosition(End + Hit.Normal * Hit.PenetrationDepth);
		}
		else
		{
			Bone.Location = CompTransform.InverseTransformPosition(Hit.Location);
		}
	};

	if (bIgnoreSelfComponent)
	{
		FHitResult Hit;
		if (World->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, TraceChannel, Shape, Params, ResponseParams))
		{
			Resolve(Hit);
		}
		return;
	}

	// Colliding with our own component is allowed here, EXCEPT against this
	// bone itself and the bones the author chose to ignore. Take the first
	// blocking hit that survives that filter.
	TArray<FHitResult> Hits;
	if (!World->SweepMultiByChannel(Hits, Start, End, FQuat::Identity, TraceChannel, Shape, Params, ResponseParams))
	{
		return;
	}

	for (const FHitResult& Hit : Hits)
	{
		if (!Hit.bBlockingHit)
		{
			continue;
		}

		bool bIgnore = false;
		if (Hit.Component == OwningComp && Hit.BoneName != NAME_None)
		{
			bIgnore = Hit.BoneName == Bone.BoneRef.BoneName;
			for (int32 i = 0; !bIgnore && i < IgnoreBones.Num(); ++i)
			{
				bIgnore = IgnoreBones[i].BoneName == Hit.BoneName;
			}
			const FString HitBoneName = Hit.BoneName.ToString();
			for (int32 i = 0; !bIgnore && i < IgnoreBoneNamePrefix.Num(); ++i)
			{
				bIgnore = HitBoneName.StartsWith(IgnoreBoneNamePrefix[i].ToString());
			}
		}

		if (!bIgnore)
		{
			Resolve(Hit);
			break;
		}
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
	KawaiiPhysics::AdjustByPlanarConstraint(Bone, ParentBone, Chain.PhysicsSettings.PlanarConstraint);
}

namespace KawaiiRelativeRotation
{
	// 0x7679920: the parent's simulated rotation, by the same rule the output
	// uses -- its pose rotation turned by its own pose->sim direction change
	// from ITS parent (the virtual parent for the chain's first bone).
	FQuat ParentSimRotation(const FKawaiiPhysicsChain& Chain, const FKawaiiPhysicsModifyBone& Parent)
	{
		const FQuat PoseRotation = Parent.PoseRotation.GetNormalized();

		const FKawaiiPhysicsModifyBone* GrandParent = nullptr;
		if (Chain.ModifyBones.IsValidIndex(Parent.ParentIndex))
		{
			GrandParent = &Chain.ModifyBones[Parent.ParentIndex];
		}
		else if (Chain.BoneSettings.bRootBoneSimulate && Parent.Index == 0 && Chain.bHasVirtualParent)
		{
			GrandParent = &Chain.VirtualParentBone;
		}
		if (!GrandParent)
		{
			return PoseRotation;
		}

		const FVector PoseVector = Parent.PoseLocation - GrandParent->PoseLocation;
		const FVector SimVector = Parent.Location - GrandParent->Location;
		if (PoseVector.IsNearlyZero(1e-4) || SimVector.IsNearlyZero(1e-4))
		{
			return PoseRotation;
		}

		const FQuat Delta = KawaiiPhysics::GameFindBetweenVectors(PoseVector.GetSafeNormal(1e-8), SimVector.GetSafeNormal(1e-8));
		return (Delta * PoseRotation).GetNormalized();
	}

	// 0x7674000: clamp the parent-local swing taking PoseDir onto SimDir, per
	// axis of its rotation vector (axis * angle), keeping a component only when
	// its sign is allowed and it is beyond +-1e-4 rad.
	bool Clamp(const FBonePhysicsParams& Params, const FVector& PoseDir, const FVector& SimDir,
	           FVector& OutDir, FQuat& OutQuat)
	{
		const FQuat Delta = KawaiiPhysics::GameFindBetweenVectors(PoseDir, SimDir).GetNormalized();
		float Angle = static_cast<float>(2.0 * FMath::Acos(FMath::Clamp(Delta.W, -1.0, 1.0)));
		const double AxisSq = Delta.X * Delta.X + Delta.Y * Delta.Y + Delta.Z * Delta.Z;
		FVector Axis = AxisSq >= 1e-8 ? FVector(Delta.X, Delta.Y, Delta.Z) / FMath::Sqrt(AxisSq) : FVector(1, 0, 0);
		if (Angle > 3.14159274f)
		{
			Angle = 6.28318548f - Angle;
			Axis = -Axis;
		}
		Axis = Axis.GetSafeNormal(1e-8);

		auto Keep = [](float Value, bool bAllowPositive, bool bAllowNegative)
		{
			return ((Value > 1e-4f && bAllowPositive) || (Value < -1e-4f && bAllowNegative)) ? Value : 0.0f;
		};
		const float X = Keep(static_cast<float>(Axis.X * Angle), Params.bAllowPositiveRelativeRotationX,
		                     Params.bAllowNegativeRelativeRotationX);
		const float Y = Keep(static_cast<float>(Axis.Y * Angle), Params.bAllowPositiveRelativeRotationY,
		                     Params.bAllowNegativeRelativeRotationY);
		const float Z = Keep(static_cast<float>(Axis.Z * Angle), Params.bAllowPositiveRelativeRotationZ,
		                     Params.bAllowNegativeRelativeRotationZ);

		const float Length = static_cast<float>(FMath::Sqrt(static_cast<double>(X) * X +
			static_cast<double>(Y) * Y + static_cast<double>(Z) * Z));
		if (Length <= 1e-4f)
		{
			OutQuat = FQuat::Identity;
			OutDir = PoseDir;
			return true;
		}

		double Sin, Cos;
		FMath::SinCos(&Sin, &Cos, static_cast<double>(Length) * 0.5);
		OutQuat = FQuat(X / Length * Sin, Y / Length * Sin, Z / Length * Sin, Cos).GetNormalized();
		OutDir = OutQuat.RotateVector(PoseDir).GetSafeNormal(1e-8);
		return !OutDir.IsNearlyZero(1e-4);
	}
}

// The game's relative-rotation limit (0x766BE80), POSITION-based: it limits
// where the bone sits relative to its parent, keeps the simulated distance,
// and caches the parent's resulting rotation for the output (consumed in
// SimulateModifyBones' orientation pass in place of the usual derivation).
void FAnimNode_KawaiiPhysics::AdjustByRelativeRotationLimit(FKawaiiPhysicsChain& Chain,
                                                            FKawaiiPhysicsModifyBone& Bone,
                                                            const FKawaiiPhysicsModifyBone& ParentBone)
{
	const FBoneChainPhysicsSettings& Settings = Chain.PhysicsSettings;
	const FBonePhysicsParams DefaultParams;
	const FBonePhysicsParams& Params = (!Settings.bUseCurve && Settings.PhysicsParamsPerBone.IsValidIndex(Bone.Index))
		                                   ? Settings.PhysicsParamsPerBone[Bone.Index]
		                                   : DefaultParams;

	if (!Params.bLimitRelativeRotation)
	{
		return;
	}
	if (Params.bAllowPositiveRelativeRotationX && Params.bAllowNegativeRelativeRotationX &&
		Params.bAllowPositiveRelativeRotationY && Params.bAllowNegativeRelativeRotationY &&
		Params.bAllowPositiveRelativeRotationZ && Params.bAllowNegativeRelativeRotationZ)
	{
		return;
	}

	const FVector PoseVector = Bone.PoseLocation - ParentBone.PoseLocation;
	const FVector SimVector = Bone.Location - ParentBone.Location;
	const float SimLength = static_cast<float>(SimVector.Size());
	if (PoseVector.IsNearlyZero(1e-4) || SimLength <= 1e-4f)
	{
		return;
	}

	const FQuat ParentPose = ParentBone.PoseRotation.GetNormalized();
	const FQuat ParentSim = KawaiiRelativeRotation::ParentSimRotation(Chain, ParentBone);

	const FVector LocalPose = ParentPose.UnrotateVector(PoseVector).GetSafeNormal(1e-8);
	const FVector LocalSim = ParentSim.UnrotateVector(SimVector * (1.0 / SimLength)).GetSafeNormal(1e-8);
	if (LocalPose.IsNearlyZero(1e-4) || LocalSim.IsNearlyZero(1e-4))
	{
		return;
	}

	FVector OutDir = LocalPose;
	FQuat OutQuat = FQuat::Identity;
	if (!KawaiiRelativeRotation::Clamp(Params, LocalPose, LocalSim, OutDir, OutQuat) || OutDir.IsNearlyZero(1e-4))
	{
		return;
	}

	Bone.Location = ParentBone.Location + ParentSim.RotateVector(OutDir) * static_cast<double>(SimLength);
	Bone.bRotationLimited = true;
	Bone.LimitedParentRotation = (ParentSim * OutQuat).GetNormalized();
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
	// As upstream: only in a game world (the Persona preview has its own edit
	// mode), only for a mesh that is actually being rendered, and only with
	// a.AnimNode.KawaiiPhysics.Debug on.
	if (!CVarAnimNodeKawaiiPhysicsDebug.GetValueOnAnyThread())
	{
		return;
	}

	FAnimInstanceProxy* Proxy = Output.AnimInstanceProxy;
	const USkeletalMeshComponent* SkelComp = Proxy->GetSkelMeshComponent();
	const UWorld* World = SkelComp ? SkelComp->GetWorld() : nullptr;
	if (!World || World->IsPreviewWorld() || !SkelComp->bRecentlyRendered)
	{
		return;
	}

	const FTransform ComponentTransform = Proxy->GetComponentTransform();
	const bool bShowLengthRate = CVarAnimNodeKawaiiPhysicsDebugLengthRate.GetValueOnAnyThread();

	for (const FKawaiiPhysicsChain& Chain : Chains)
	{
		for (const FKawaiiPhysicsModifyBone& Bone : Chain.ModifyBones)
		{
			const FVector LocationWS = ComponentTransform.TransformPosition(Bone.Location);
			Proxy->AnimDrawDebugSphere(LocationWS, Bone.PhysicsSettings.Radius, 8,
			                           Bone.bDummy ? FColor::Red : FColor::Yellow, false, -1, 0, SDPG_Foreground);

			if (bShowLengthRate)
			{
				// Labelled at the bone, as upstream -- not an on-screen list.
				Proxy->AnimDrawDebugInWorldMessage(FString::Printf(TEXT("%.2f"), Bone.LengthRateFromRoot),
				                                   Bone.Location, FColor::White, 1.0f);
			}
		}
	}

	// Collision limits: orange authored on the node, blue from a data asset.
	auto DrawSpheres = [&](const TArray<FSphericalLimit>& Limits, const FColor& Colour)
	{
		for (const FSphericalLimit& Limit : Limits)
		{
			Proxy->AnimDrawDebugSphere(ComponentTransform.TransformPosition(Limit.Location), Limit.Radius, 8,
			                           Colour, false, -1, 0, SDPG_Foreground);
		}
	};
	DrawSpheres(SphericalLimits, FColor::Orange);
	DrawSpheres(SphericalLimitsData, FColor::Blue);

	// Upstream draws capsules here only from UE 5.4 (AnimDrawDebugCapsule), and
	// has no box drawing yet; this targets 5.3, so neither is drawn.
}
#endif
