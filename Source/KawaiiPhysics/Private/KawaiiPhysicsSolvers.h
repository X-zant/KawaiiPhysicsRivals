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
	 * Places a limit in component space for this frame.
	 *
	 * bUseParentCompBone resolves the limit against the component this mesh is
	 * attached to rather than against this mesh, which is how the game drives
	 * cosmetic attachments off the wearer's skeleton.
	 */
	template <typename LimitType>
	void ResolveLimitTransform(LimitType& Limit, FComponentSpacePoseContext& Output,
	                           const FBoneContainer& BoneContainer, const FTransform& ComponentTransform,
	                           const FTransform& ParentCompTransform)
	{
		if (Limit.bUseParentCompBone)
		{
			// Without a resolved parent skeleton the best anchor available is the
			// parent component's own transform.
			const FTransform ParentInComponentSpace = ParentCompTransform.GetRelativeTransform(ComponentTransform);
			Limit.Location = ParentInComponentSpace.TransformPosition(Limit.OffsetLocation);
			Limit.Rotation = ParentInComponentSpace.GetRotation() * Limit.OffsetRotation.Quaternion();
			return;
		}

		const FCompactPoseBoneIndex CompactPoseIndex = Limit.DrivingBone.IsValidToEvaluate(BoneContainer)
			                                              ? Limit.DrivingBone.GetCompactPoseIndex(BoneContainer)
			                                              : FCompactPoseBoneIndex(INDEX_NONE);

		if (CompactPoseIndex < 0)
		{
			// An unbound limit sits at the component origin, offset as authored.
			Limit.Location = Limit.OffsetLocation;
			Limit.Rotation = Limit.OffsetRotation.Quaternion();
			return;
		}

		const FTransform BoneTransform = Output.Pose.GetComponentSpaceTransform(CompactPoseIndex);
		Limit.Location = BoneTransform.TransformPosition(Limit.OffsetLocation);
		Limit.Rotation = BoneTransform.GetRotation() * Limit.OffsetRotation.Quaternion();
	}

	/** Resolves every limit array on a node, including the planar plane equations. */
	inline void ResolveAllLimits(TArray<FSphericalLimit>& Spherical, TArray<FCapsuleLimit>& Capsule,
	                             TArray<FBoxLimit>& Box, TArray<FPlanarLimit>& Planar,
	                             FComponentSpacePoseContext& Output, const FBoneContainer& BoneContainer,
	                             const FTransform& ComponentTransform, const FTransform& ParentCompTransform)
	{
		for (FSphericalLimit& Limit : Spherical)
		{
			ResolveLimitTransform(Limit, Output, BoneContainer, ComponentTransform, ParentCompTransform);
		}
		for (FCapsuleLimit& Limit : Capsule)
		{
			ResolveLimitTransform(Limit, Output, BoneContainer, ComponentTransform, ParentCompTransform);
		}
		for (FBoxLimit& Limit : Box)
		{
			ResolveLimitTransform(Limit, Output, BoneContainer, ComponentTransform, ParentCompTransform);
		}
		for (FPlanarLimit& Limit : Planar)
		{
			ResolveLimitTransform(Limit, Output, BoneContainer, ComponentTransform, ParentCompTransform);

			// The plane travels with its driving bone: normal from the rotated up
			// axis, through the resolved location.
			Limit.Plane = FPlane(Limit.Location, Limit.Rotation.GetAxisZ());
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

	inline void AdjustBySphereCollision(FKawaiiPhysicsModifyBone& Bone, const TArray<FSphericalLimit>& Limits)
	{
		for (const FSphericalLimit& Limit : Limits)
		{
			if (!Limit.bEnable || Limit.Radius <= 0.0f)
			{
				continue;
			}

			if (Limit.LimitType == ESphericalLimitType::Outer)
			{
				const float CombinedRadius = Limit.Radius + Bone.PhysicsSettings.Radius;
				if ((Bone.Location - Limit.Location).SizeSquared() > FMath::Square(CombinedRadius))
				{
					continue;
				}

				Bone.Location = Limit.Location + (Bone.Location - Limit.Location).GetSafeNormal() * CombinedRadius;
			}
			else
			{
				// Keep the bone inside; usable radius shrinks by the bone's own.
				const float InnerRadius = FMath::Max(0.0f, Limit.Radius - Bone.PhysicsSettings.Radius);
				if ((Bone.Location - Limit.Location).SizeSquared() < FMath::Square(InnerRadius))
				{
					continue;
				}

				Bone.Location = Limit.Location + (Bone.Location - Limit.Location).GetSafeNormal() * InnerRadius;
			}
		}
	}

	inline void AdjustByCapsuleCollision(FKawaiiPhysicsModifyBone& Bone, const TArray<FCapsuleLimit>& Limits)
	{
		for (const FCapsuleLimit& Limit : Limits)
		{
			if (!Limit.bEnable || Limit.Radius <= 0.0f)
			{
				continue;
			}

			// Length is the cylindrical section only, so the segment spans half
			// of it either side of the resolved centre.
			const FVector Axis = Limit.Rotation.GetAxisZ();
			const FVector SegmentStart = Limit.Location - Axis * (Limit.Length * 0.5f);
			const FVector SegmentEnd = Limit.Location + Axis * (Limit.Length * 0.5f);

			const FVector ClosestPoint = ClosestPointOnSegment(Bone.Location, SegmentStart, SegmentEnd);

			const float CombinedRadius = Limit.Radius + Bone.PhysicsSettings.Radius;
			if ((Bone.Location - ClosestPoint).SizeSquared() > FMath::Square(CombinedRadius))
			{
				continue;
			}

			FVector Direction = (Bone.Location - ClosestPoint).GetSafeNormal();
			if (Direction.IsNearlyZero())
			{
				// Exactly on the axis: any perpendicular will do to get it out.
				Direction = Axis.GetSafeNormal();
				Direction = FVector::CrossProduct(Direction, FMath::Abs(Direction.Z) < 0.9f
					                                             ? FVector::UpVector
					                                             : FVector::ForwardVector).GetSafeNormal();
				if (Direction.IsNearlyZero())
				{
					Direction = FVector::UpVector;
				}
			}

			Bone.Location = ClosestPoint + Direction * CombinedRadius;
		}
	}

	inline void AdjustByBoxCollision(FKawaiiPhysicsModifyBone& Bone, const TArray<FBoxLimit>& Limits)
	{
		for (const FBoxLimit& Limit : Limits)
		{
			if (!Limit.bEnable || Limit.Extent.IsNearlyZero())
			{
				continue;
			}

			const FTransform BoxTransform(Limit.Rotation, Limit.Location);
			const FVector LocalLocation = BoxTransform.InverseTransformPosition(Bone.Location);
			const FVector Extent = Limit.Extent + FVector(Bone.PhysicsSettings.Radius);

			if (FMath::Abs(LocalLocation.X) > Extent.X ||
				FMath::Abs(LocalLocation.Y) > Extent.Y ||
				FMath::Abs(LocalLocation.Z) > Extent.Z)
			{
				continue;
			}

			// Inside: leave by the nearest face, which is the smallest push-out.
			const FVector Penetration(
				Extent.X - FMath::Abs(LocalLocation.X),
				Extent.Y - FMath::Abs(LocalLocation.Y),
				Extent.Z - FMath::Abs(LocalLocation.Z));

			auto SignOrPositive = [](double Value) { return Value < 0.0 ? -1.0 : 1.0; };

			FVector Resolved = LocalLocation;
			if (Penetration.X <= Penetration.Y && Penetration.X <= Penetration.Z)
			{
				Resolved.X = SignOrPositive(LocalLocation.X) * Extent.X;
			}
			else if (Penetration.Y <= Penetration.Z)
			{
				Resolved.Y = SignOrPositive(LocalLocation.Y) * Extent.Y;
			}
			else
			{
				Resolved.Z = SignOrPositive(LocalLocation.Z) * Extent.Z;
			}

			Bone.Location = BoxTransform.TransformPosition(Resolved);
		}
	}

	inline void AdjustByPlanerCollision(FKawaiiPhysicsModifyBone& Bone, const TArray<FPlanarLimit>& Limits)
	{
		for (const FPlanarLimit& Limit : Limits)
		{
			if (!Limit.bEnable)
			{
				continue;
			}

			const float Distance = Limit.Plane.PlaneDot(Bone.Location);
			if (Distance >= 0.0f)
			{
				continue;
			}

			Bone.Location -= Limit.Plane.GetSafeNormal() * Distance;
		}
	}

	/** Clamps the bone's direction from its parent to a cone around the pose direction. */
	inline void AdjustByAngleLimit(FKawaiiPhysicsModifyBone& Bone, const FKawaiiPhysicsModifyBone& ParentBone)
	{
		const float LimitAngle = Bone.PhysicsSettings.LimitAngle;
		if (LimitAngle <= 0.0f)
		{
			return;
		}

		const FVector PoseDirection = (Bone.PoseLocation - ParentBone.PoseLocation).GetSafeNormal();
		const FVector SimDirection = (Bone.Location - ParentBone.Location).GetSafeNormal();

		if (PoseDirection.IsNearlyZero() || SimDirection.IsNearlyZero())
		{
			return;
		}

		const float AngleRadians = FMath::Acos(FMath::Clamp(
			static_cast<float>(FVector::DotProduct(PoseDirection, SimDirection)), -1.0f, 1.0f));
		const float LimitRadians = FMath::DegreesToRadians(LimitAngle);

		if (AngleRadians <= LimitRadians)
		{
			return;
		}

		const FVector Axis = FVector::CrossProduct(PoseDirection, SimDirection).GetSafeNormal();
		if (Axis.IsNearlyZero())
		{
			return;
		}

		const FVector Clamped = PoseDirection.RotateAngleAxisRad(LimitRadians, Axis);
		const float Length = (Bone.Location - ParentBone.Location).Size();
		Bone.Location = ParentBone.Location + Clamped * Length;
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

	/** Pins the bone to the plane through its pose position, normal to the chosen axis. */
	inline void AdjustByPlanarConstraint(FKawaiiPhysicsModifyBone& Bone, EPlanarConstraint Constraint)
	{
		if (Constraint == EPlanarConstraint::None)
		{
			return;
		}

		FPlane Plane;
		switch (Constraint)
		{
		case EPlanarConstraint::X:
			Plane = FPlane(Bone.PoseLocation, FVector::ForwardVector);
			break;
		case EPlanarConstraint::Y:
			Plane = FPlane(Bone.PoseLocation, FVector::RightVector);
			break;
		case EPlanarConstraint::Z:
			Plane = FPlane(Bone.PoseLocation, FVector::UpVector);
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
		case EXPBDComplianceType::Wood: return 0.0000000016f;
		case EXPBDComplianceType::Leather: return 0.0000001f;
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
		// A pinned bone has infinite mass and absorbs none of the correction.
		const float InvMass1 = Bone1.IsSimulated() ? 1.0f : 0.0f;
		const float InvMass2 = Bone2.IsSimulated() ? 1.0f : 0.0f;
		const float InvMassSum = InvMass1 + InvMass2;

		if (InvMassSum <= 0.0f)
		{
			return;
		}

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
}
