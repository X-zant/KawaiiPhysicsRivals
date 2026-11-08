// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License
//
// Collision response and positional limits, as free functions.
//
// Both nodes need identical behaviour here, and none of it depends on node
// state -- only on the bone being corrected and the limits it is tested
// against. Keeping it out of the node classes means the Merge node gets the
// same solver rather than a second, drifting copy.

#pragma once

#include "AnimNode_KawaiiPhysics.h"

namespace KawaiiPhysics
{
	/** The game's FindBetweenVectors (0xF3A370): stock UE's, except the anti-parallel fallback picks its axis by
	 *  the LARGEST component of A. */
	inline FQuat GameFindBetweenVectors(const FVector& A, const FVector& B)
	{
		const double NormAB = FMath::Sqrt(A.SizeSquared() * B.SizeSquared());
		const double W = NormAB + FVector::DotProduct(A, B);
		FQuat Result;
		if (W >= 1e-6 * NormAB)
		{
			const FVector Cross = FVector::CrossProduct(A, B);
			Result = FQuat(Cross.X, Cross.Y, Cross.Z, W);
		}
		else if (FMath::Abs(A.X) > FMath::Abs(A.Y) && FMath::Abs(A.X) > FMath::Abs(A.Z))
		{
			Result = FQuat(-A.Z, 0.0, A.X, 0.0);
		}
		else
		{
			Result = FQuat(0.0, -A.Z, A.Y, 0.0);
		}
		return Result.GetNormalized();
	}

	/**
	 * Rescales a per-frame rate to the actual frame time.
	 *
	 * Damping and stiffness are authored as "fraction removed per frame at the
	 * target framerate". Applying that fraction directly at a different frame
	 * time changes the behaviour, so convert it to the equivalent fraction for
	 * this frame: 1 - (1 - rate)^exponent.
	 *
	 * Lives here rather than in each node's .cpp because unity builds
	 * concatenate translation units, and two anonymous-namespace copies of the
	 * same function collide.
	 */
	FORCEINLINE float AdjustRateByExponent(float Rate, float Exponent)
	{
		const float Clamped = FMath::Clamp(Rate, 0.0f, 1.0f);
		return 1.0f - FMath::Pow(1.0f - Clamped, Exponent);
	}

	/** Multiplies Value by a curve sample, leaving it untouched if unauthored. */
	FORCEINLINE float EvalCurveMultiply(const FRuntimeFloatCurve& CurveData, float Time, float Value)
	{
		const FRichCurve* Curve = CurveData.GetRichCurveConst();
		return (Curve && Curve->GetNumKeys() > 0) ? Value * Curve->Eval(Time) : Value;
	}

	/**
	 * Places a sphere, capsule or box limit in component space for this frame,
	 * as the game does (main node 0x7689A40 / 0x7683140 / 0x7680980; Merge
	 * 0x768BF10 / 0x7685640 / 0x7682E50). bEnable is OVERWRITTEN every frame: 1
	 * when the limit could be placed, 0 when it could not (its location then
	 * stays where it was). The offset is in the driving bone's space.
	 * bMainNode: the main node also honours bUseParentCompBone (the bone
	 * ParentDrivingBoneName on the parent skeletal mesh, cached on the game
	 * thread in ParentBonePlacement) and records the driving bone's scale X,
	 * which its collisions multiply the limit's size by. The Merge node does
	 * neither.
	 */
	template <typename LimitType>
	void ResolveLimitTransform(LimitType& Limit, FComponentSpacePoseContext& Output,
	                           const FBoneContainer& BoneContainer, bool bMainNode)
	{
		if (bMainNode && Limit.bUseParentCompBone)
		{
			if (!Limit.bParentBonePlacementValid)
			{
				Limit.bEnable = false;
				return;
			}
			const FTransform Offset(Limit.OffsetRotation.Quaternion(), Limit.OffsetLocation);
			const FTransform Placed = Offset * Limit.ParentBonePlacement;
			Limit.Location = Placed.GetLocation();
			Limit.Rotation = Placed.GetRotation();
			Limit.DrivingBoneScale = static_cast<float>(Placed.GetScale3D().X);
			Limit.bEnable = true;
			return;
		}

		const FCompactPoseBoneIndex CompactPoseIndex = Limit.DrivingBone.IsValidToEvaluate(BoneContainer)
		                                              ? Limit.DrivingBone.GetCompactPoseIndex(BoneContainer)
		                                              : FCompactPoseBoneIndex(INDEX_NONE);
		if (CompactPoseIndex < 0)
		{
			Limit.bEnable = false;
			return;
		}

		const FTransform BoneTransform = Output.Pose.GetComponentSpaceTransform(CompactPoseIndex);
		Limit.Location = BoneTransform.TransformPosition(Limit.OffsetLocation);
		Limit.Rotation = BoneTransform.GetRotation() * Limit.OffsetRotation.Quaternion();
		if (bMainNode)
		{
			Limit.DrivingBoneScale = static_cast<float>(BoneTransform.GetScale3D().X);
		}
		Limit.bEnable = true;
	}

	/**
	 * Places a planar limit (game 0x7688C70, shared by both nodes): no parent-
	 * component path, no scale. With no driving bone it sits at its offset and
	 * bEnable is left as it is.
	 */
	inline void ResolvePlanarLimitTransform(FPlanarLimit& Limit, FComponentSpacePoseContext& Output,
	                                        const FBoneContainer& BoneContainer)
	{
		const FCompactPoseBoneIndex CompactPoseIndex = Limit.DrivingBone.IsValidToEvaluate(BoneContainer)
		                                              ? Limit.DrivingBone.GetCompactPoseIndex(BoneContainer)
		                                              : FCompactPoseBoneIndex(INDEX_NONE);
		if (CompactPoseIndex >= 0)
		{
			const FTransform BoneTransform = Output.Pose.GetComponentSpaceTransform(CompactPoseIndex);
			Limit.Location = BoneTransform.TransformPosition(Limit.OffsetLocation);
			Limit.Rotation = (BoneTransform.GetRotation() * Limit.OffsetRotation.Quaternion()).GetNormalized(1e-8f);
			Limit.bEnable = true;
		}
		else
		{
			Limit.Location = Limit.OffsetLocation;
			Limit.Rotation = Limit.OffsetRotation.Quaternion().GetNormalized(1e-8f);
		}

		// Normal = the rotated up axis, through the resolved location.
		Limit.Plane = FPlane(Limit.Location, Limit.Rotation.GetUpVector());
	}

	/** Resolves every limit array on a node. */
	inline void ResolveAllLimits(TArray<FSphericalLimit>& Spherical, TArray<FCapsuleLimit>& Capsule,
	                             TArray<FBoxLimit>& Box, TArray<FPlanarLimit>& Planar,
	                             FComponentSpacePoseContext& Output, const FBoneContainer& BoneContainer,
	                             bool bMainNode)
	{
		for (FSphericalLimit& Limit : Spherical)
		{
			ResolveLimitTransform(Limit, Output, BoneContainer, bMainNode);
		}
		for (FCapsuleLimit& Limit : Capsule)
		{
			ResolveLimitTransform(Limit, Output, BoneContainer, bMainNode);
		}
		for (FBoxLimit& Limit : Box)
		{
			ResolveLimitTransform(Limit, Output, BoneContainer, bMainNode);
		}
		for (FPlanarLimit& Limit : Planar)
		{
			ResolvePlanarLimitTransform(Limit, Output, BoneContainer);
		}
	}

	/** Closest point to P on the segment AB. */
	inline FVector ClosestPointOnSegment(const FVector& P, const FVector& A, const FVector& B)
	{
		const FVector AB = B - A;
		const float LengthSq = AB.SizeSquared();
		if (LengthSq <= KINDA_SMALL_NUMBER)
		{
			return A;
		}

		const float T = FMath::Clamp(FVector::DotProduct(P - A, AB) / LengthSq, 0.0f, 1.0f);
		return A + AB * T;
	}

	// The collision and limit solvers below reproduce upstream v1.18.0 exactly,
	// quirks included. The game's merge point is at or before v1.18.0 (its
	// schema has none of the properties upstream added from v1.19 on), so this
	// is the code the game runs. Where a quirk looks like an upstream oversight
	// it is called out, not fixed -- fixing it would make the preview diverge
	// from the game.

	/** Sphere collision (main 0x766C7E0; Merge 0x766CB00 is the same without the scale). The radius test
	 *  happens before scaling. Inner tests Radius + BoneRadius but clamps to Radius - BoneRadius, as upstream. */
	inline void AdjustBySphereCollision(FKawaiiPhysicsModifyBone& Bone, const TArray<FSphericalLimit>& Limits,
	                                    bool bScaleByBone = false)
	{
		for (const FSphericalLimit& Sphere : Limits)
		{
			if (!Sphere.bEnable || Sphere.Radius <= 0.0f)
			{
				continue;
			}

			const float Radius = bScaleByBone ? Sphere.Radius * Sphere.DrivingBoneScale : Sphere.Radius;
			const float LimitDistance = Bone.PhysicsSettings.Radius + Radius;
			const FVector Offset = Bone.Location - Sphere.Location;
			if (Sphere.LimitType == ESphericalLimitType::Outer)
			{
				if (Offset.SizeSquared() > LimitDistance * LimitDistance)
				{
					continue;
				}
				Bone.Location += Offset.GetSafeNormal() * (LimitDistance - Offset.Size());
			}
			else
			{
				if (Offset.SizeSquared() < LimitDistance * LimitDistance)
				{
					continue;
				}
				Bone.Location = Sphere.Location + Offset.GetSafeNormal() * (Radius - Bone.PhysicsSettings.Radius);
			}
		}
	}

	/** Capsule collision with the bone as a point (main 0x766A990, Merge 0x766AFE0 without the scale). */
	inline void AdjustByCapsuleCollision(FKawaiiPhysicsModifyBone& Bone, const TArray<FCapsuleLimit>& Limits,
	                                     bool bScaleByBone = false)
	{
		for (const FCapsuleLimit& Capsule : Limits)
		{
			if (!Capsule.bEnable || Capsule.Radius <= 0 || Capsule.Length <= 0)
			{
				continue;
			}

			const float Scale = bScaleByBone ? Capsule.DrivingBoneScale : 1.0f;
			const FVector StartPoint = Capsule.Location + Capsule.Rotation.GetAxisZ() * Capsule.Length * Scale * 0.5f;
			const FVector EndPoint = Capsule.Location + Capsule.Rotation.GetAxisZ() * Capsule.Length * Scale * -0.5f;
			const float DistSquared = FMath::PointDistToSegmentSquared(Bone.Location, StartPoint, EndPoint);

			const float LimitDistance = Bone.PhysicsSettings.Radius + Capsule.Radius * Scale;
			if (DistSquared < LimitDistance * LimitDistance)
			{
				const FVector ClosestPoint = FMath::ClosestPointOnSegment(Bone.Location, StartPoint, EndPoint);
				Bone.Location = ClosestPoint + (Bone.Location - ClosestPoint).GetSafeNormal() * LimitDistance;
			}
		}
	}

	/** Box collision. The main node (0x7669D70) checks bEnable and scales the extent by the driving bone's
	 *  scale; the Merge node (0x766A380) does neither, so a disabled box still collides there. */
	inline void AdjustByBoxCollision(FKawaiiPhysicsModifyBone& Bone, const TArray<FBoxLimit>& Limits,
	                                 bool bMainNode = false)
	{
		for (const FBoxLimit& Box : Limits)
		{
			if (bMainNode && !Box.bEnable)
			{
				continue;
			}

			const FTransform BoxTransform(Box.Rotation, Box.Location);
			const float SphereRadius = Bone.PhysicsSettings.Radius;
			const FVector LocalSphereCenter = BoxTransform.InverseTransformPosition(Bone.Location);
			const FVector Extent = bMainNode ? Box.Extent * Box.DrivingBoneScale : Box.Extent;
			const FBox LocalBox(-Extent, Extent);

			if (FMath::SphereAABBIntersection(FSphere(LocalSphereCenter, SphereRadius), LocalBox))
			{
				FVector ClosestPoint = LocalSphereCenter;
				ClosestPoint.X = FMath::Clamp(ClosestPoint.X, LocalBox.Min.X, LocalBox.Max.X);
				ClosestPoint.Y = FMath::Clamp(ClosestPoint.Y, LocalBox.Min.Y, LocalBox.Max.Y);
				ClosestPoint.Z = FMath::Clamp(ClosestPoint.Z, LocalBox.Min.Z, LocalBox.Max.Z);

				FVector PushOutVector = LocalSphereCenter - ClosestPoint;
				float Distance = PushOutVector.Size();

				// Centre inside the box: upstream pushes away from the box's
				// centre by the sphere radius, which does not necessarily clear
				// the box. Reproduced as is.
				if (PushOutVector.IsNearlyZero())
				{
					PushOutVector = LocalSphereCenter;
					Distance = SphereRadius;
				}

				if (Distance <= SphereRadius)
				{
					const FVector PushOutDirection = PushOutVector.GetSafeNormal();
					const FVector NewLocalSphereCenter = ClosestPoint + PushOutDirection * SphereRadius;
					Bone.Location = BoxTransform.TransformPosition(NewLocalSphereCenter);
				}
			}
		}
	}

	inline void AdjustByPlanerCollision(FKawaiiPhysicsModifyBone& Bone, const TArray<FPlanarLimit>& Limits)
	{
		for (const FPlanarLimit& Planar : Limits)
		{
			if (!Planar.bEnable)
			{
				continue;
			}

			const FVector PointOnPlane = FVector::PointPlaneProject(Bone.Location, Planar.Plane);
			const float DistSquared = (Bone.Location - PointOnPlane).SizeSquared();

			// Also catches a bone that tunnelled through the plane this frame.
			FVector IntersectionPoint;
			if (DistSquared < Bone.PhysicsSettings.Radius * Bone.PhysicsSettings.Radius ||
				FMath::SegmentPlaneIntersection(Bone.Location, Bone.PrevLocation, Planar.Plane, IntersectionPoint))
			{
				Bone.Location = PointOnPlane + Planar.Rotation.GetUpVector() * Bone.PhysicsSettings.Radius;
			}
		}
	}

	/** Clamps the bone's direction from its parent to a cone around the pose direction. */
	/** The game's angle limit (0x76697A0, shared): rotate the simulated direction back onto the cone, keeping
	 *  the simulated length. No early-out for a degenerate axis: anti-parallel directions rotate about a zero
	 *  axis, which only scales the direction by cos(angle), as the game does. */
	inline void AdjustByAngleLimit(FKawaiiPhysicsModifyBone& Bone, const FKawaiiPhysicsModifyBone& ParentBone)
	{
		const float LimitAngle = Bone.PhysicsSettings.LimitAngle;
		if (LimitAngle <= 0.0f)
		{
			return;
		}

		const FVector SimDirection = (Bone.Location - ParentBone.Location).GetSafeNormal(1e-8);
		const FVector PoseDirection = (Bone.PoseLocation - ParentBone.PoseLocation).GetSafeNormal(1e-8);
		const FVector Cross = FVector::CrossProduct(PoseDirection, SimDirection);
		const float Angle = static_cast<float>(FMath::Atan2(Cross.Size(), FVector::DotProduct(PoseDirection, SimDirection))) *
			57.2958f;
		if (Angle - LimitAngle <= 0.0f)
		{
			return;
		}

		const FVector Axis = Cross.GetSafeNormal(1e-8);
		const double Length = (Bone.Location - ParentBone.Location).Size();
		Bone.Location = ParentBone.Location + SimDirection.RotateAngleAxis(LimitAngle - Angle, Axis) * Length;
	}

	/** Clamps how far the bone may stray from its animated position. */
	inline void AdjustByLinearLimit(FKawaiiPhysicsModifyBone& Bone)
	{
		const float LimitLinear = Bone.PhysicsSettings.LimitLinear;
		if (LimitLinear <= 0.0f)
		{
			return;
		}

		const FVector Offset = Bone.Location - Bone.PoseLocation;
		if (Offset.SizeSquared() <= FMath::Square(LimitLinear))
		{
			return;
		}

		Bone.Location = Bone.PoseLocation + Offset.GetSafeNormal() * LimitLinear;
	}

	/**
	 * Pins the bone to a plane through its PARENT's current location, whose normal
	 * is the chosen axis of the parent's pose rotation -- upstream's definition.
	 */
	inline void AdjustByPlanarConstraint(FKawaiiPhysicsModifyBone& Bone, const FKawaiiPhysicsModifyBone& ParentBone,
	                                     EPlanarConstraint Constraint)
	{
		FPlane Plane;
		switch (Constraint)
		{
		case EPlanarConstraint::X:
			Plane = FPlane(ParentBone.Location, ParentBone.PoseRotation.GetAxisX());
			break;
		case EPlanarConstraint::Y:
			Plane = FPlane(ParentBone.Location, ParentBone.PoseRotation.GetAxisY());
			break;
		case EPlanarConstraint::Z:
			Plane = FPlane(ParentBone.Location, ParentBone.PoseRotation.GetAxisZ());
			break;
		default:
			return;
		}

		Bone.Location = FVector::PointPlaneProject(Bone.Location, Plane);
	}

	/**
	 * XPBD compliance in metres per newton, from Macklin's XPBD slides.
	 * The solver divides by dt^2, so these are the raw material constants.
	 */
	inline float GetComplianceValue(EXPBDComplianceType Type)
	{
		switch (Type)
		{
		case EXPBDComplianceType::Concrete: return 0.00000000004f;
		case EXPBDComplianceType::Wood: return 0.00000000016f;   // 0.16e-9, as upstream
		case EXPBDComplianceType::Leather: return 0.000000001f;   // 1.0e-9, as upstream
		case EXPBDComplianceType::Tendon: return 0.000000002f;
		case EXPBDComplianceType::Rubber: return 0.0000001f;
		case EXPBDComplianceType::Muscle: return 0.00002f;
		case EXPBDComplianceType::Fat: return 0.0001f;
		default: return 0.0f;
		}
	}

	/**
	 * One XPBD distance-constraint iteration between two bones.
	 * Lambda accumulates across the iterations of a single frame.
	 */
	inline void SolveDistanceConstraint(FKawaiiPhysicsModifyBone& Bone1, FKawaiiPhysicsModifyBone& Bone2,
	                                    float RestLength, float AlphaTilde, float& Lambda)
	{
		// Unit mass on both ends, ALWAYS -- including a pinned or root bone.
		// Upstream's solve uses a fixed mass sum of 2 and moves both bones;
		// it does not special-case pinned ones. Matching it, even though
		// infinite mass for a pinned bone would be the more "physical" choice,
		// because the game runs upstream's version.
		constexpr float InvMass1 = 1.0f;
		constexpr float InvMass2 = 1.0f;
		constexpr float InvMassSum = InvMass1 + InvMass2;

		const FVector Delta = Bone1.Location - Bone2.Location;
		const float CurrentLength = Delta.Size();
		if (CurrentLength <= KINDA_SMALL_NUMBER)
		{
			return;
		}

		const FVector Direction = Delta / CurrentLength;
		const float Error = CurrentLength - RestLength;

		const float DeltaLambda = (-Error - AlphaTilde * Lambda) / (InvMassSum + AlphaTilde);
		Lambda += DeltaLambda;

		const FVector Correction = Direction * DeltaLambda;
		Bone1.Location += Correction * InvMass1;
		Bone2.Location -= Correction * InvMass2;
	}
/** The fix-tail stiffness re-solve (game 0x767BF71-0x767CD0E); see FAnimNode_KawaiiPhysics::SimulateModifyBones. */
	inline void FixTailStiffnessSweep(TArray<FKawaiiPhysicsModifyBone>& Bones, const float Total,
	                                  const FVector& FollowLocation, const float Exponent)
{
	const int32 Num = Bones.Num();

	auto Slot = [](const TArray<FVector>& Array, int32 Index)
	{
		return Array.IsValidIndex(Index) ? Array[Index] : FVector::ZeroVector;
	};

	// Forward: toward the parent's simulated location + the pose offset.
	TArray<FVector> Forward;
	Forward.Init(FVector::ZeroVector, Num);
	for (int32 Index = 0; Index < Num; ++Index)
	{
		const FKawaiiPhysicsModifyBone& Bone = Bones[Index];
		if (Bone.bSkipSimulate)
		{
			continue;
		}

		FVector Target = Bone.PoseLocation;
		if (Bones.IsValidIndex(Bone.ParentIndex))
		{
			const FKawaiiPhysicsModifyBone& Parent = Bones[Bone.ParentIndex];
			Target = Parent.Location + Slot(Forward, Index - 1) + (Bone.PoseLocation - Parent.PoseLocation);
		}
		Forward[Index] = (Target - Bone.Location) * AdjustRateByExponent(Bone.PhysicsSettings.Stiffness, Exponent);
	}

	// Backward: toward the first child's simulated location - the pose offset.
	TArray<FVector> Backward;
	Backward.Init(FVector::ZeroVector, Num);
	for (int32 Index = Num - 1; Index >= 0; --Index)
	{
		const FKawaiiPhysicsModifyBone& Bone = Bones[Index];

		// Checked before the skip test: the pinned fixed bone moves to the follow bone.
		if (Bone.bFixed && Bone.bHasFollowBone)
		{
			Backward[Index] = FollowLocation - Bone.PoseLocation;
		}

		if (Bone.bSkipSimulate || Bone.ChildIndices.IsEmpty() || !Bones.IsValidIndex(Bone.ChildIndices[0]))
		{
			continue;
		}

		const FKawaiiPhysicsModifyBone& Child = Bones[Bone.ChildIndices[0]];
		const FVector Target = Child.Location + Slot(Backward, Index + 1) + (Bone.PoseLocation - Child.PoseLocation);
		Backward[Index] = (Target - Bone.Location) * AdjustRateByExponent(Bone.PhysicsSettings.Stiffness, Exponent);
	}

	// Blend. The game runs this for EVERY bone (no skip test), so a fixed bone
	// with a follow bone is carried onto it.
	if (Total <= 0.0f)
	{
		return;   // the game divides by zero here; a zero-length chain has nothing to blend
	}
	for (int32 Index = 0; Index < Num; ++Index)
	{
		const float W = (Total - Bones[Index].LengthFromRoot) / Total;
		Bones[Index].Location += Backward[Index] * (1.0f - W) + Forward[Index] * W;
	}
}

/** The fix-tail length re-solve (game 0x767D446-0x767E2D6). Virtual: the simulated root's virtual parent, or null. */
	inline void FixTailLengthSweep(TArray<FKawaiiPhysicsModifyBone>& Bones, const float Total,
	                               const FKawaiiPhysicsModifyBone* Virtual)
{
	const int32 Num = Bones.Num();

	auto Slot = [](const TArray<FVector>& Array, int32 Index)
	{
		return Array.IsValidIndex(Index) ? Array[Index] : FVector::ZeroVector;
	};

	// Forward: exact pose length from the parent (the virtual parent for a simulated root).
	TArray<FVector> Forward;
	Forward.Init(FVector::ZeroVector, Num);
	for (int32 Index = 0; Index < Num; ++Index)
	{
		const FKawaiiPhysicsModifyBone& Bone = Bones[Index];
		if (Bone.bSkipSimulate)
		{
			continue;
		}

		const bool bVirtual = !Bone.HasParent();
		if (bVirtual && !Virtual)
		{
			continue;
		}
		if (!bVirtual && !Bones.IsValidIndex(Bone.ParentIndex))
		{
			continue;
		}

		const FKawaiiPhysicsModifyBone& Parent = bVirtual ? *Virtual : Bones[Bone.ParentIndex];
		const float PoseLength = (Bone.PoseLocation - Parent.PoseLocation).Size();
		const FVector Anchor = bVirtual ? Parent.Location : Parent.Location + Slot(Forward, Index - 1);
		Forward[Index] = (Bone.Location - Anchor).GetSafeNormal(1e-8f) * PoseLength + Anchor - Bone.Location;
	}

	// Backward: exact pose length from the first child.
	TArray<FVector> Backward;
	Backward.Init(FVector::ZeroVector, Num);
	for (int32 Index = Num - 1; Index >= 0; --Index)
	{
		const FKawaiiPhysicsModifyBone& Bone = Bones[Index];
		if (Bone.bSkipSimulate || Bone.ChildIndices.IsEmpty() || !Bones.IsValidIndex(Bone.ChildIndices[0]))
		{
			continue;
		}

		const FKawaiiPhysicsModifyBone& Child = Bones[Bone.ChildIndices[0]];
		const float PoseLength = (Bone.PoseLocation - Child.PoseLocation).Size();
		const FVector Anchor = Child.Location + Slot(Backward, Index + 1);
		Backward[Index] = (Bone.Location - Anchor).GetSafeNormal(1e-8f) * PoseLength + Anchor - Bone.Location;
	}

	if (Total <= 0.0f)
	{
		return;
	}
	for (int32 Index = 0; Index < Num; ++Index)
	{
		if (Bones[Index].bSkipSimulate)
		{
			continue;   // unlike the stiffness blend, this one skips
		}
		const float W = (Total - Bones[Index].LengthFromRoot) / Total;
		Bones[Index].Location += Backward[Index] * (1.0f - W) + Forward[Index] * W;
	}
}
}
