// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License
//
// The Merge node runs the same solver as FAnimNode_KawaiiPhysics, but resolves
// every chain against one shared set of limits and one shared constraint pass.

#include "AnimNode_KawaiiPhysicsMerge.h"

#include "KawaiiPhysics.h"
#include "KawaiiPhysicsBoneConstraintsDataAsset.h"
#include "KawaiiPhysicsCustomExternalForce.h"
#include "KawaiiPhysicsExternalForce.h"
#include "KawaiiPhysicsLimitsDataAsset.h"
#include "KawaiiPhysicsSolvers.h"

#include "Animation/AnimInstanceProxy.h"
#include "Components/SkeletalMeshComponent.h"
#include "SceneInterface.h"

// Shared with FAnimNode_KawaiiPhysics. These deliberately are NOT local copies:
// unity builds concatenate translation units, so a second anonymous-namespace
// definition of the same helper is a redefinition error.
using KawaiiPhysics::AdjustRateByExponent;
using KawaiiPhysics::EvalCurveMultiply;

namespace
{
	FORCEINLINE FVector GetForwardVector(const FQuat& Rotation, EBoneForwardAxis ForwardAxis)
	{
		return FAnimNode_KawaiiPhysics::GetBoneForwardVector(Rotation, ForwardAxis);
	}
}

FAnimNode_KawaiiPhysicsMerge::FAnimNode_KawaiiPhysicsMerge()
{
	Nodes.AddDefaulted();
}

void FAnimNode_KawaiiPhysicsMerge::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	Super::Initialize_AnyThread(Context);

	for (FSingleNodeParameter& Param : Nodes)
	{
		Param.bInitialized = false;
		Param.ModifyBones.Empty();
	}

	for (FInstancedStruct& ForceStruct : ExternalForces)
	{
		if (FKawaiiPhysics_ExternalForce* Force = ForceStruct.GetMutablePtr<FKawaiiPhysics_ExternalForce>())
		{
			Force->Initialize(Context);
		}
	}

	bInitPhysicsSettings = false;
	bResetDynamics = false;
}

void FAnimNode_KawaiiPhysicsMerge::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	Super::CacheBones_AnyThread(Context);

	for (FSingleNodeParameter& Param : Nodes)
	{
		Param.bInitialized = false;
	}
}

void FAnimNode_KawaiiPhysicsMerge::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
	for (FSingleNodeParameter& Param : Nodes)
	{
		Param.RootBone.Initialize(RequiredBones);
		Param.FixedBone.Initialize(RequiredBones);
		Param.FollowBone.Initialize(RequiredBones);

		for (FBoneReference& Bone : Param.ExcludeBones)
		{
			Bone.Initialize(RequiredBones);
		}

		for (FKawaiiPhysicsRootBoneSetting& Additional : Param.AdditionalRootBones)
		{
			Additional.RootBone.Initialize(RequiredBones);
			for (FBoneReference& Bone : Additional.OverrideExcludeBones)
			{
				Bone.Initialize(RequiredBones);
			}
		}

		for (FBoneShape& Shape : Param.CustomShapes)
		{
			Shape.DrivingBone.Initialize(RequiredBones);
		}
	}

	for (FModifyBoneConstraint& Constraint : BoneConstraints)
	{
		Constraint.InitializeBone(RequiredBones);
	}

	BoneConstraintsData.Reset();
	if (BoneConstraintsDataAsset)
	{
		BoneConstraintsData = BoneConstraintsDataAsset->GenerateBoneConstraints();
		for (FModifyBoneConstraint& Constraint : BoneConstraintsData)
		{
			Constraint.InitializeBone(RequiredBones);
		}
	}

	for (FBoneReference& Bone : IgnoreBones)
	{
		Bone.Initialize(RequiredBones);
	}

	SphericalLimitsData.Reset();
	CapsuleLimitsData.Reset();
	BoxLimitsData.Reset();
	PlanarLimitsData.Reset();

	if (LimitsDataAsset)
	{
		SphericalLimitsData = LimitsDataAsset->SphericalLimits;
		CapsuleLimitsData = LimitsDataAsset->CapsuleLimits;
		BoxLimitsData = LimitsDataAsset->BoxLimits;
		PlanarLimitsData = LimitsDataAsset->PlanarLimits;
	}

	auto InitLimits = [&RequiredBones](auto& Limits)
	{
		for (auto& Limit : Limits)
		{
			Limit.DrivingBone.Initialize(RequiredBones);
		}
	};

	InitLimits(SphericalLimits);
	InitLimits(CapsuleLimits);
	InitLimits(BoxLimits);
	InitLimits(PlanarLimits);
	InitLimits(SphericalLimitsData);
	InitLimits(CapsuleLimitsData);
	InitLimits(BoxLimitsData);
	InitLimits(PlanarLimitsData);
}

bool FAnimNode_KawaiiPhysicsMerge::IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
#if ENABLE_ANIM_DEBUG
	if (!CVarAnimNodeKawaiiPhysicsEnable.GetValueOnAnyThread())
	{
		return false;
	}
#endif

	for (const FSingleNodeParameter& Param : Nodes)
	{
		if (Param.RootBone.IsValidToEvaluate(RequiredBones))
		{
			return true;
		}
	}

	return false;
}

bool FAnimNode_KawaiiPhysicsMerge::HasPreUpdate() const
{
	return true;
}

void FAnimNode_KawaiiPhysicsMerge::PreUpdate(const UAnimInstance* InAnimInstance)
{
	ParentSkelComp = InAnimInstance ? InAnimInstance->GetSkelMeshComponent() : nullptr;

	if (!ParentSkelComp)
	{
		return;
	}

	CachedLODLevel = ParentSkelComp->GetPredictedLODLevel();

	const ENetRole OwnerRole = ParentSkelComp->GetOwner()
		                           ? ParentSkelComp->GetOwner()->GetLocalRole()
		                           : ROLE_None;
	for (FSingleNodeParameter& Param : Nodes)
	{
		Param.CachedOwnerNetRole = OwnerRole;
	}
}

void FAnimNode_KawaiiPhysicsMerge::UpdateInternal(const FAnimationUpdateContext& Context)
{
	Super::UpdateInternal(Context);

	DeltaTime = Context.GetDeltaTime();
}

void FAnimNode_KawaiiPhysicsMerge::ResetDynamics(ETeleportType InTeleportType)
{
	Super::ResetDynamics(InTeleportType);

	bResetDynamics = true;
}

void FAnimNode_KawaiiPhysicsMerge::GatherDebugData(FNodeDebugData& DebugData)
{
	FString DebugLine = DebugData.GetNodeName(this);

	int32 TotalBones = 0;
	for (const FSingleNodeParameter& Param : Nodes)
	{
		TotalBones += Param.ModifyBones.Num();
	}

	DebugLine += FString::Printf(TEXT("(MergedChains: %d, SimulatedBones: %d)"), Nodes.Num(), TotalBones);
	DebugData.AddDebugItem(DebugLine);

	ComponentPose.GatherDebugData(DebugData);
}

///////////////////////////////////////////////////////////////////////////////
// Setup
///////////////////////////////////////////////////////////////////////////////

int32 FAnimNode_KawaiiPhysicsMerge::AddModifyBone(FSingleNodeParameter& Param,
                                                  TArray<FKawaiiPhysicsModifyBone>& InModifyBones,
                                                  FComponentSpacePoseContext& Output,
                                                  const FBoneContainer& BoneContainer,
                                                  const FReferenceSkeleton& RefSkeleton, int32 BoneIndex,
                                                  const TArray<FBoneReference>& InExcludeBones)
{
	if (BoneIndex < 0 || BoneIndex >= RefSkeleton.GetNum())
	{
		return INDEX_NONE;
	}

	const FName BoneName = RefSkeleton.GetBoneName(BoneIndex);
	if (InExcludeBones.ContainsByPredicate([&BoneName](const FBoneReference& Ref) { return Ref.BoneName == BoneName; }))
	{
		return INDEX_NONE;
	}

	FKawaiiPhysicsModifyBone NewBone;
	NewBone.BoneRef.BoneName = BoneName;
	NewBone.BoneRef.Initialize(BoneContainer);

	if (!NewBone.BoneRef.IsValidToEvaluate(BoneContainer))
	{
		return INDEX_NONE;
	}

	const FCompactPoseBoneIndex CompactPoseIndex = NewBone.BoneRef.GetCompactPoseIndex(BoneContainer);
	if (CompactPoseIndex < 0)
	{
		return INDEX_NONE;
	}

	const int32 NewIndex = InModifyBones.Add(NewBone);
	FKawaiiPhysicsModifyBone& Bone = InModifyBones[NewIndex];
	Bone.Index = NewIndex;

	const FTransform ComponentSpaceTransform = Output.Pose.GetComponentSpaceTransform(CompactPoseIndex);
	Bone.Location = ComponentSpaceTransform.GetLocation();
	Bone.PrevLocation = Bone.Location;
	Bone.PoseLocation = Bone.Location;
	Bone.Rotation = ComponentSpaceTransform.GetRotation();
	Bone.PrevRotation = Bone.Rotation;
	Bone.PoseRotation = Bone.Rotation;
	Bone.PoseScale = ComponentSpaceTransform.GetScale3D();

	TArray<int32> Children;
	const int32 NumBones = RefSkeleton.GetNum();
	for (int32 ChildIndex = BoneIndex + 1; ChildIndex < NumBones; ++ChildIndex)
	{
		if (BoneIndex == RefSkeleton.GetParentIndex(ChildIndex))
		{
			Children.Add(ChildIndex);
		}
	}

	if (Children.Num() == 0)
	{
		if (Param.DummyBoneLength > 0.0f)
		{
			FKawaiiPhysicsModifyBone DummyBone;
			DummyBone.BoneRef = InModifyBones[NewIndex].BoneRef;
			DummyBone.bDummy = true;
			DummyBone.Location = InModifyBones[NewIndex].Location +
				GetForwardVector(InModifyBones[NewIndex].Rotation, Param.BoneForwardAxis) * Param.DummyBoneLength;
			DummyBone.PrevLocation = DummyBone.Location;
			DummyBone.PoseLocation = DummyBone.Location;
			DummyBone.Rotation = InModifyBones[NewIndex].Rotation;
			DummyBone.PrevRotation = DummyBone.Rotation;
			DummyBone.PoseRotation = DummyBone.Rotation;
			DummyBone.PoseScale = InModifyBones[NewIndex].PoseScale;

			const int32 DummyIndex = InModifyBones.Add(DummyBone);
			InModifyBones[DummyIndex].Index = DummyIndex;
			InModifyBones[DummyIndex].ParentIndex = NewIndex;
			InModifyBones[NewIndex].ChildIndices.Add(DummyIndex);
		}
	}
	else
	{
		for (const int32 ChildBoneIndex : Children)
		{
			const int32 ChildModifyIndex =
				AddModifyBone(Param, InModifyBones, Output, BoneContainer, RefSkeleton, ChildBoneIndex, InExcludeBones);
			if (ChildModifyIndex != INDEX_NONE)
			{
				InModifyBones[ChildModifyIndex].ParentIndex = NewIndex;
				InModifyBones[NewIndex].ChildIndices.Add(ChildModifyIndex);
			}
		}
	}

	return NewIndex;
}

void FAnimNode_KawaiiPhysicsMerge::CalcBoneLength(FSingleNodeParameter& Param, FKawaiiPhysicsModifyBone& Bone,
                                                  const TArray<FTransform>& RefBonePose)
{
	if (!Bone.HasParent())
	{
		Bone.LengthFromRoot = 0.0f;
	}
	else
	{
		const FKawaiiPhysicsModifyBone& Parent = Param.ModifyBones[Bone.ParentIndex];
		Bone.LengthFromRoot = Parent.LengthFromRoot + (Bone.PoseLocation - Parent.PoseLocation).Size();
	}

	Param.TotalBoneLength = FMath::Max(Param.TotalBoneLength, Bone.LengthFromRoot);

	for (const int32 ChildIndex : Bone.ChildIndices)
	{
		CalcBoneLength(Param, Param.ModifyBones[ChildIndex], RefBonePose);
	}
}

void FAnimNode_KawaiiPhysicsMerge::InitModifyBones(FSingleNodeParameter& Param, FComponentSpacePoseContext& Output,
                                                   const FBoneContainer& BoneContainer)
{
	const FReferenceSkeleton& RefSkeleton = BoneContainer.GetReferenceSkeleton();

	Param.ModifyBones.Reset();
	Param.TotalBoneLength = 0.0f;
	Param.FollowBoneIndex = INDEX_NONE;

	const int32 RootBoneIndex = RefSkeleton.FindBoneIndex(Param.RootBone.BoneName);
	if (RootBoneIndex != INDEX_NONE)
	{
		AddModifyBone(Param, Param.ModifyBones, Output, BoneContainer, RefSkeleton, RootBoneIndex, Param.ExcludeBones);
	}

	for (const FKawaiiPhysicsRootBoneSetting& Additional : Param.AdditionalRootBones)
	{
		const int32 AdditionalIndex = RefSkeleton.FindBoneIndex(Additional.RootBone.BoneName);
		if (AdditionalIndex == INDEX_NONE)
		{
			continue;
		}

		const TArray<FBoneReference>& Excludes = Additional.bUseOverrideExcludeBones
			                                         ? Additional.OverrideExcludeBones
			                                         : Param.ExcludeBones;
		AddModifyBone(Param, Param.ModifyBones, Output, BoneContainer, RefSkeleton, AdditionalIndex, Excludes);
	}

	if (Param.ModifyBones.IsEmpty())
	{
		Param.bInitialized = true;
		return;
	}

	const TArray<FTransform>& RefBonePose = RefSkeleton.GetRefBonePose();
	for (FKawaiiPhysicsModifyBone& Bone : Param.ModifyBones)
	{
		if (!Bone.HasParent())
		{
			CalcBoneLength(Param, Bone, RefBonePose);
		}
	}

	for (FKawaiiPhysicsModifyBone& Bone : Param.ModifyBones)
	{
		Bone.LengthRateFromRoot = Param.TotalBoneLength > 0.0f ? Bone.LengthFromRoot / Param.TotalBoneLength : 0.0f;

		Bone.bFixed = false;
		Bone.bHasFollowBone = false;

		if (!Bone.HasParent() && !Param.bRootBoneSimulate)
		{
			Bone.bFixed = true;
		}

		if (Param.bShouldFixTailBone && !Bone.bDummy)
		{
			const bool bHasRealChild = Bone.ChildIndices.ContainsByPredicate(
				[&Param](int32 Index) { return !Param.ModifyBones[Index].bDummy; });
			if (!bHasRealChild)
			{
				Bone.bFixed = true;
			}
		}

		if (Param.FixedBone.BoneName != NAME_None && Bone.BoneRef.BoneName == Param.FixedBone.BoneName)
		{
			Bone.bFixed = true;
		}

		if (Param.FollowBone.BoneName != NAME_None && Bone.BoneRef.BoneName == Param.FollowBone.BoneName)
		{
			Bone.bHasFollowBone = true;
			Param.FollowBoneIndex = Bone.Index;
		}
	}

	Param.bInitialized = true;
	bInitPhysicsSettings = false;
}

void FAnimNode_KawaiiPhysicsMerge::InitBoneConstraints()
{
	MergedBoneConstraints.Reset();
	MergedBoneConstraints.Append(BoneConstraints);
	MergedBoneConstraints.Append(BoneConstraintsData);

	// Constraints here may span chains, so bone lookup walks every chain and the
	// resolved index is packed as (ChainIndex << 16) | BoneIndex.
	auto FindBone = [this](const FName BoneName) -> int32
	{
		for (int32 ChainIndex = 0; ChainIndex < Nodes.Num(); ++ChainIndex)
		{
			const int32 BoneIndex = Nodes[ChainIndex].ModifyBones.IndexOfByPredicate(
				[BoneName](const FKawaiiPhysicsModifyBone& Bone)
				{
					return !Bone.bDummy && Bone.BoneRef.BoneName == BoneName;
				});

			if (BoneIndex != INDEX_NONE)
			{
				return (ChainIndex << 16) | BoneIndex;
			}
		}

		return INDEX_NONE;
	};

	for (FModifyBoneConstraint& Constraint : MergedBoneConstraints)
	{
		Constraint.ModifyBoneIndex1 = FindBone(Constraint.Bone1.BoneName);
		Constraint.ModifyBoneIndex2 = FindBone(Constraint.Bone2.BoneName);
		Constraint.Lambda = 0.0f;

		if (!Constraint.IsBoneReferenceValid())
		{
			Constraint.Length = -1.0f;
			continue;
		}

		const FKawaiiPhysicsModifyBone& Bone1 =
			Nodes[Constraint.ModifyBoneIndex1 >> 16].ModifyBones[Constraint.ModifyBoneIndex1 & 0xFFFF];
		const FKawaiiPhysicsModifyBone& Bone2 =
			Nodes[Constraint.ModifyBoneIndex2 >> 16].ModifyBones[Constraint.ModifyBoneIndex2 & 0xFFFF];

		Constraint.Length = (Bone1.PoseLocation - Bone2.PoseLocation).Size();
	}
}

FKawaiiPhysicsModifyBone* FAnimNode_KawaiiPhysicsMerge::ResolvePackedBone(int32 PackedIndex)
{
	if (PackedIndex == INDEX_NONE)
	{
		return nullptr;
	}

	const int32 ChainIndex = PackedIndex >> 16;
	const int32 BoneIndex = PackedIndex & 0xFFFF;

	if (!Nodes.IsValidIndex(ChainIndex) || !Nodes[ChainIndex].ModifyBones.IsValidIndex(BoneIndex))
	{
		return nullptr;
	}

	return &Nodes[ChainIndex].ModifyBones[BoneIndex];
}

void FAnimNode_KawaiiPhysicsMerge::AdjustByBoneConstraintsMerged(int32 IterationCount)
{
	if (IterationCount <= 0 || MergedBoneConstraints.IsEmpty() || DeltaTime <= 0.0f)
	{
		return;
	}

	const float DeltaTimeSq = DeltaTime * DeltaTime;

	for (FModifyBoneConstraint& Constraint : MergedBoneConstraints)
	{
		Constraint.Lambda = 0.0f;
	}

	for (int32 Iteration = 0; Iteration < IterationCount; ++Iteration)
	{
		for (FModifyBoneConstraint& Constraint : MergedBoneConstraints)
		{
			if (!Constraint.IsBoneReferenceValid() || !Constraint.IsValid())
			{
				continue;
			}

			FKawaiiPhysicsModifyBone* Bone1 = ResolvePackedBone(Constraint.ModifyBoneIndex1);
			FKawaiiPhysicsModifyBone* Bone2 = ResolvePackedBone(Constraint.ModifyBoneIndex2);

			if (!Bone1 || !Bone2)
			{
				continue;
			}

			const EXPBDComplianceType Compliance = Constraint.bOverrideCompliance
				                                       ? Constraint.ComplianceType
				                                       : BoneConstraintGlobalComplianceType;
			const float AlphaTilde = KawaiiPhysics::GetComplianceValue(Compliance) / DeltaTimeSq;

			KawaiiPhysics::SolveDistanceConstraint(*Bone1, *Bone2, Constraint.Length, AlphaTilde, Constraint.Lambda);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////
// Per-frame
///////////////////////////////////////////////////////////////////////////////

void FAnimNode_KawaiiPhysicsMerge::UpdatePhysicsSettingsOfModifyBones(FSingleNodeParameter& Param)
{
	for (FKawaiiPhysicsModifyBone& Bone : Param.ModifyBones)
	{
		const float Rate = Bone.LengthRateFromRoot;
		FKawaiiPhysicsSettings& Out = Bone.PhysicsSettings;

		Out = Param.PhysicsSettings;
		Out.Damping = EvalCurveMultiply(DampingCurveData, Rate, Param.PhysicsSettings.Damping);
		Out.Stiffness = EvalCurveMultiply(StiffnessCurveData, Rate, Param.PhysicsSettings.Stiffness);
		Out.WorldDampingLocation = EvalCurveMultiply(WorldDampingLocationCurveData, Rate,
		                                     Param.PhysicsSettings.WorldDampingLocation);
		Out.WorldDampingRotation = EvalCurveMultiply(WorldDampingRotationCurveData, Rate,
		                                     Param.PhysicsSettings.WorldDampingRotation);
		Out.Radius = EvalCurveMultiply(RadiusCurveData, Rate, Param.PhysicsSettings.Radius);
		Out.LimitAngle = EvalCurveMultiply(LimitAngleCurveData, Rate, Param.PhysicsSettings.LimitAngle);
		Out.LimitLinear = EvalCurveMultiply(LimitLinearCurveData, Rate, Param.PhysicsSettings.LimitLinear);
		Out.GravityScale = EvalCurveMultiply(GravityCurveData, Rate, Param.PhysicsSettings.GravityScale);

		Out.Damping = FMath::Clamp(Out.Damping, 0.0f, 1.0f);
		Out.Stiffness = FMath::Clamp(Out.Stiffness, 0.0f, 1.0f);
		Out.WorldDampingLocation = FMath::Clamp(Out.WorldDampingLocation, 0.0f, 1.0f);
		Out.WorldDampingRotation = FMath::Clamp(Out.WorldDampingRotation, 0.0f, 1.0f);
		Out.Radius = FMath::Max(0.0f, Out.Radius);
	}
}

void FAnimNode_KawaiiPhysicsMerge::UpdateModifyBonesPoseTransform(FSingleNodeParameter& Param,
                                                                  FComponentSpacePoseContext& Output,
                                                                  const FBoneContainer& BoneContainer)
{
	for (FKawaiiPhysicsModifyBone& Bone : Param.ModifyBones)
	{
		if (!Bone.bDummy)
		{
			Bone.UpdatePoseTransform(BoneContainer, Output.Pose, ResetBoneTransformWhenBoneNotFound);
			continue;
		}

		if (Bone.HasParent())
		{
			const FKawaiiPhysicsModifyBone& Parent = Param.ModifyBones[Bone.ParentIndex];
			Bone.PoseLocation = Parent.PoseLocation +
				GetForwardVector(Parent.PoseRotation, Param.BoneForwardAxis) * Param.DummyBoneLength;
			Bone.PoseRotation = Parent.PoseRotation;
			Bone.PoseScale = Parent.PoseScale;
		}
	}
}

void FAnimNode_KawaiiPhysicsMerge::UpdateSkelCompMove(const FTransform& ComponentTransform)
{
	SkelCompMoveVector = ComponentTransform.InverseTransformPosition(PreSkelCompTransform.GetLocation());
	SkelCompMoveRotation = ComponentTransform.InverseTransformRotation(PreSkelCompTransform.GetRotation());

	if (TeleportDistanceThreshold > 0.0f &&
		SkelCompMoveVector.SizeSquared() > FMath::Square(TeleportDistanceThreshold))
	{
		SkelCompMoveVector = FVector::ZeroVector;
	}

	if (TeleportRotationThreshold > 0.0f &&
		FMath::RadiansToDegrees(SkelCompMoveRotation.GetAngle()) > TeleportRotationThreshold)
	{
		SkelCompMoveRotation = FQuat::Identity;
	}
}

void FAnimNode_KawaiiPhysicsMerge::SimulateModifyBones(FSingleNodeParameter& Param,
                                                       FComponentSpacePoseContext& Output,
                                                       const FTransform& ComponentTransform)
{
	if (DeltaTime <= 0.0f || Param.ModifyBones.IsEmpty())
	{
		return;
	}

	const float Exponent = TargetFramerate > 0 ? DeltaTime * TargetFramerate : 1.0f;
	const FVector GravityCS = ComponentTransform.InverseTransformVector(Gravity);

	for (FKawaiiPhysicsModifyBone& Bone : Param.ModifyBones)
	{
		if (!Bone.IsSimulated())
		{
			Bone.Location = Bone.PoseLocation;
			Bone.PrevLocation = Bone.PoseLocation;
			continue;
		}

		const FKawaiiPhysicsSettings& Settings = Bone.PhysicsSettings;

		FVector Velocity = FVector::ZeroVector;
		if (DeltaTimeOld > 0.0f)
		{
			Velocity = (Bone.Location - Bone.PrevLocation) / DeltaTimeOld;
		}

		Bone.PrevLocation = Bone.Location;
		Velocity *= 1.0f - AdjustRateByExponent(Settings.Damping, Exponent);

		Bone.Location += Velocity * DeltaTime;
		Bone.Location += GravityCS * Settings.GravityScale * DeltaTime * DeltaTime;
		Bone.Location += SkelCompMoveVector * (1.0f - AdjustRateByExponent(Settings.WorldDampingLocation, Exponent));
		Bone.Location += (SkelCompMoveRotation.RotateVector(Bone.Location) - Bone.Location) *
			(1.0f - AdjustRateByExponent(Settings.WorldDampingRotation, Exponent));
		Bone.Location += (Bone.PoseLocation - Bone.Location) * AdjustRateByExponent(Settings.Stiffness, Exponent);

		// NOTE: the FInstancedStruct force presets on this node are serialized
		// (they are part of the game's schema) but not evaluated here. Their
		// virtuals are declared against FAnimNode_KawaiiPhysics and a chain, and
		// this node has neither. Gravity and wind, which is what the Merge node
		// is normally set up with, are applied directly above and below.
	}

	if (bEnableWind && ParentSkelComp && ParentSkelComp->GetScene())
	{
		const FSceneInterface* Scene = ParentSkelComp->GetScene();

		for (FKawaiiPhysicsModifyBone& Bone : Param.ModifyBones)
		{
			if (!Bone.IsSimulated())
			{
				continue;
			}

			const FVector WorldLocation = ComponentTransform.TransformPosition(Bone.Location);

			FVector WindDirection = FVector::ZeroVector;
			float WindSpeed = 0.0f;
			float WindMinGust = 0.0f;
			float WindMaxGust = 0.0f;
			Scene->GetWindParameters_GameThread(WorldLocation, WindDirection, WindSpeed, WindMinGust, WindMaxGust);

			const FVector WindCS =
				ComponentTransform.InverseTransformVector(WindDirection * WindSpeed * WindScale);
			Bone.Location += WindCS * DeltaTime * DeltaTime;
		}
	}

	// Orientation, derived from the final positions.
	for (FKawaiiPhysicsModifyBone& Bone : Param.ModifyBones)
	{
		if (Bone.bDummy || Bone.ChildIndices.IsEmpty())
		{
			continue;
		}

		FVector ChildCentre = FVector::ZeroVector;
		for (const int32 ChildIndex : Bone.ChildIndices)
		{
			ChildCentre += Param.ModifyBones[ChildIndex].Location;
		}
		ChildCentre /= static_cast<float>(Bone.ChildIndices.Num());

		const FVector PoseDirection = GetForwardVector(Bone.PoseRotation, Param.BoneForwardAxis);
		const FVector SimDirection = (ChildCentre - Bone.Location).GetSafeNormal();

		if (!SimDirection.IsNearlyZero() && !PoseDirection.IsNearlyZero())
		{
			Bone.PrevRotation = Bone.Rotation;
			Bone.Rotation = FQuat::FindBetweenNormals(PoseDirection, SimDirection) * Bone.PoseRotation;
		}
	}
}

void FAnimNode_KawaiiPhysicsMerge::ApplySimulateResult(FSingleNodeParameter& Param,
                                                       FComponentSpacePoseContext& Output,
                                                       const FBoneContainer& BoneContainer,
                                                       TArray<FBoneTransform>& OutBoneTransforms)
{
	for (const FKawaiiPhysicsModifyBone& Bone : Param.ModifyBones)
	{
		if (Bone.bDummy || Bone.bBoneMissing)
		{
			continue;
		}

		const FCompactPoseBoneIndex CompactPoseIndex = Bone.BoneRef.GetCompactPoseIndex(BoneContainer);
		if (CompactPoseIndex < 0)
		{
			continue;
		}

		if (OutBoneTransforms.ContainsByPredicate(
			[CompactPoseIndex](const FBoneTransform& Transform) { return Transform.BoneIndex == CompactPoseIndex; }))
		{
			continue;
		}

		FTransform NewTransform = Output.Pose.GetComponentSpaceTransform(CompactPoseIndex);
		NewTransform.SetLocation(Bone.Location);
		NewTransform.SetRotation(Bone.Rotation);

		OutBoneTransforms.Add(FBoneTransform(CompactPoseIndex, NewTransform));
	}
}

void FAnimNode_KawaiiPhysicsMerge::WarmUp(FComponentSpacePoseContext& Output, const FBoneContainer& BoneContainer,
                                          FTransform& ComponentTransform)
{
	const float WarmUpDeltaTime = 1.0f / FMath::Max(1, TargetFramerate);

	const float SavedDeltaTime = DeltaTime;
	const float SavedDeltaTimeOld = DeltaTimeOld;
	const FVector SavedMoveVector = SkelCompMoveVector;
	const FQuat SavedMoveRotation = SkelCompMoveRotation;

	DeltaTime = WarmUpDeltaTime;
	DeltaTimeOld = WarmUpDeltaTime;
	SkelCompMoveVector = FVector::ZeroVector;
	SkelCompMoveRotation = FQuat::Identity;

	for (int32 Frame = 0; Frame < WarmUpFrames; ++Frame)
	{
		for (FSingleNodeParameter& Param : Nodes)
		{
			if (Param.IsEnabledForLOD(CachedLODLevel) && !Param.ModifyBones.IsEmpty())
			{
				SimulateModifyBones(Param, Output, ComponentTransform);
			}
		}
	}

	DeltaTime = SavedDeltaTime;
	DeltaTimeOld = SavedDeltaTimeOld;
	SkelCompMoveVector = SavedMoveVector;
	SkelCompMoveRotation = SavedMoveRotation;
}

void FAnimNode_KawaiiPhysicsMerge::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output,
                                                                     TArray<FBoneTransform>& OutBoneTransforms)
{
	const FBoneContainer& BoneContainer = Output.Pose.GetPose().GetBoneContainer();
	FTransform ComponentTransform = Output.AnimInstanceProxy->GetComponentTransform();

#if WITH_EDITORONLY_DATA
	LastEvaluatedTime = FPlatformTime::Seconds();
#endif

	UpdateSkelCompMove(ComponentTransform);

	// The whole point of this node: one limit resolution shared by every chain.
	const FTransform ParentCompTransform = ParentSkelComp && ParentSkelComp->GetAttachParent()
		                                       ? ParentSkelComp->GetAttachParent()->GetComponentTransform()
		                                       : ComponentTransform;

	KawaiiPhysics::ResolveAllLimits(SphericalLimits, CapsuleLimits, BoxLimits, PlanarLimits,
	                                Output, BoneContainer, ComponentTransform, ParentCompTransform);
	KawaiiPhysics::ResolveAllLimits(SphericalLimitsData, CapsuleLimitsData, BoxLimitsData, PlanarLimitsData,
	                                Output, BoneContainer, ComponentTransform, ParentCompTransform);

	bool bAnyRebuilt = false;

	for (FSingleNodeParameter& Param : Nodes)
	{
		if (!Param.IsEnabledForLOD(CachedLODLevel) || !Param.RootBone.IsValidToEvaluate(BoneContainer))
		{
			continue;
		}

		if (!Param.bInitialized || Param.ModifyBones.IsEmpty())
		{
			InitModifyBones(Param, Output, BoneContainer);
			bAnyRebuilt = true;
			if (Param.ModifyBones.IsEmpty())
			{
				continue;
			}
		}

		UpdateModifyBonesPoseTransform(Param, Output, BoneContainer);

		if (bResetDynamics)
		{
			for (FKawaiiPhysicsModifyBone& Bone : Param.ModifyBones)
			{
				Bone.Location = Bone.PoseLocation;
				Bone.PrevLocation = Bone.PoseLocation;
				Bone.Rotation = Bone.PoseRotation;
				Bone.PrevRotation = Bone.PoseRotation;
			}
		}

		if (bUpdatePhysicsSettingsInGame || !bInitPhysicsSettings)
		{
			UpdatePhysicsSettingsOfModifyBones(Param);
		}
	}

	// Constraints span chains, so they are rebuilt only once every chain is up.
	if (bAnyRebuilt)
	{
		InitBoneConstraints();
	}

	const bool bWantWarmUp = (bNeedWarmUp && WarmUpFrames > 0) &&
		(!bInitPhysicsSettings || (bResetDynamics && bUseWarmUpWhenResetDynamics));

	if (bWantWarmUp)
	{
		WarmUp(Output, BoneContainer, ComponentTransform);
	}
	else
	{
		for (FSingleNodeParameter& Param : Nodes)
		{
			if (Param.IsEnabledForLOD(CachedLODLevel) && !Param.ModifyBones.IsEmpty())
			{
				SimulateModifyBones(Param, Output, ComponentTransform);
			}
		}
	}

	// Constraints, then collision, then constraints again -- all across every
	// chain at once, which is what makes this cheaper than N separate nodes.
	AdjustByBoneConstraintsMerged(BoneConstraintIterationCountBeforeCollision);

	for (FSingleNodeParameter& Param : Nodes)
	{
		if (!Param.IsEnabledForLOD(CachedLODLevel))
		{
			continue;
		}

		for (FKawaiiPhysicsModifyBone& Bone : Param.ModifyBones)
		{
			if (!Bone.IsSimulated())
			{
				continue;
			}

			KawaiiPhysics::AdjustBySphereCollision(Bone, SphericalLimits);
			KawaiiPhysics::AdjustBySphereCollision(Bone, SphericalLimitsData);
			KawaiiPhysics::AdjustByCapsuleCollision(Bone, CapsuleLimits);
			KawaiiPhysics::AdjustByCapsuleCollision(Bone, CapsuleLimitsData);
			KawaiiPhysics::AdjustByBoxCollision(Bone, BoxLimits);
			KawaiiPhysics::AdjustByBoxCollision(Bone, BoxLimitsData);
			KawaiiPhysics::AdjustByPlanerCollision(Bone, PlanarLimits);
			KawaiiPhysics::AdjustByPlanerCollision(Bone, PlanarLimitsData);

			if (Bone.HasParent())
			{
				KawaiiPhysics::AdjustByAngleLimit(Bone, Param.ModifyBones[Bone.ParentIndex]);
			}

			KawaiiPhysics::AdjustByLinearLimit(Bone);
			KawaiiPhysics::AdjustByPlanarConstraint(Bone, PlanarConstraint);
		}
	}

	AdjustByBoneConstraintsMerged(BoneConstraintIterationCountAfterCollision);

	for (FSingleNodeParameter& Param : Nodes)
	{
		if (Param.IsEnabledForLOD(CachedLODLevel) && !Param.ModifyBones.IsEmpty())
		{
			ApplySimulateResult(Param, Output, BoneContainer, OutBoneTransforms);
		}
	}

	bInitPhysicsSettings = true;
	bResetDynamics = false;
	DeltaTimeOld = DeltaTime;
	PreSkelCompTransform = ComponentTransform;

	OutBoneTransforms.Sort(FCompareBoneTransformIndex());
}
