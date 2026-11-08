// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License
//
// The Merge node, following the game's compiled FAnimNode_KawaiiPhysicsMerge
// (S9.5: vtable 0xC851A78, EvaluateSkeletalControl 0x7674F60). Several chains
// share one set of limits; its bone constraints are serialized but, as in the
// game, never solved.

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

	// The game's Initialize (0x7677A30) empties a COPY of each chain, so the
	// chains themselves keep their bones; reproduced by not touching them.

	for (FInstancedStruct& ForceStruct : ExternalForces)
	{
		if (FKawaiiPhysics_ExternalForce* Force = ForceStruct.GetMutablePtr<FKawaiiPhysics_ExternalForce>())
		{
			Force->Initialize(Context);
		}
	}

	bInitPhysicsSettings = false;
	bResetDynamics = false;
	DeltaTimeOld = TargetFramerate > 0 ? 1.0f / TargetFramerate : 0.0f;
}

void FAnimNode_KawaiiPhysicsMerge::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	// A new bone container re-resolves the bones in place (InitializeBoneReferences);
	// it does not rebuild the chains.
	Super::CacheBones_AnyThread(Context);
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

		for (FKawaiiPhysicsModifyBone& ModifyBone : Param.ModifyBones)
		{
			ModifyBone.BoneRef.Initialize(RequiredBones);
		}
		Param.FollowBoneRef.Initialize(RequiredBones);
		if (Param.bRootBoneSimulate)
		{
			Param.RootParentRef.Initialize(RequiredBones);
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

	// The Merge node builds limits from PhysicsAssetForLimits too (game 0x7671BE0).
	FAnimNode_KawaiiPhysics::BuildPhysicsAssetLimits(PhysicsAssetForLimits, RequiredBones, SphericalLimitsData,
	                                                 CapsuleLimitsData, BoxLimitsData);

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

	// Game 0x7677ED0 only checks that the bone NAMES are valid, which is always
	// true in practice; the real gate is inside the evaluation.
	return true;
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

	// Game 0x76798B0.
	bResetDynamics |= (InTeleportType == ETeleportType::ResetPhysics);
	if (bUseWarmUpWhenResetDynamics)
	{
		bNeedWarmUp = true;
	}
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
// Setup -- the game's AddModifyBone 0x76691C0 / InitModifyBones 0x7667780 /
// CalcBoneLength 0x7673680
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

	const FCompactPoseBoneIndex CompactPoseIndex = NewBone.BoneRef.GetCompactPoseIndex(BoneContainer);
	if (!NewBone.BoneRef.IsValidToEvaluate(BoneContainer) || CompactPoseIndex < 0)
	{
		return INDEX_NONE;
	}

	TArray<int32> Children;
	for (int32 ChildIndex = BoneIndex + 1; ChildIndex < RefSkeleton.GetNum(); ++ChildIndex)
	{
		if (BoneIndex == RefSkeleton.GetParentIndex(ChildIndex))
		{
			Children.Add(ChildIndex);
		}
	}

	// The only fixed bone: FixedBone, with bShouldFixTailBone, when it has no
	// skeleton children. It follows FollowBone when that exists.
	if (Param.bShouldFixTailBone && Param.FixedBone.BoneName != NAME_None && BoneName == Param.FixedBone.BoneName &&
		Children.Num() == 0)
	{
		NewBone.bFixed = true;
		Param.FollowBoneRef = Param.FollowBone;
		Param.FollowBoneRef.Initialize(BoneContainer);

		const int32 FollowIndex = RefSkeleton.FindBoneIndex(Param.FollowBone.BoneName);
		if (FollowIndex != INDEX_NONE)
		{
			auto RefComponentSpace = [&RefSkeleton](int32 Index)
			{
				FTransform Result = FTransform::Identity;
				for (int32 Walk = Index; Walk != INDEX_NONE; Walk = RefSkeleton.GetParentIndex(Walk))
				{
					Result = Result * RefSkeleton.GetRefBonePose()[Walk];
				}
				return Result;
			};

			NewBone.bHasFollowBone = true;
			Param.FixedRefRelToFollow = RefComponentSpace(BoneIndex).GetRelativeTransform(RefComponentSpace(FollowIndex));
		}
	}

	const FTransform ComponentSpaceTransform = Output.Pose.GetComponentSpaceTransform(CompactPoseIndex);
	NewBone.Location = ComponentSpaceTransform.GetLocation();
	NewBone.PrevLocation = NewBone.Location;
	NewBone.PoseLocation = NewBone.Location;
	NewBone.Rotation = ComponentSpaceTransform.GetRotation();
	NewBone.PrevRotation = NewBone.Rotation;
	NewBone.PoseRotation = NewBone.Rotation;
	NewBone.PoseScale = ComponentSpaceTransform.GetScale3D();

	const int32 NewIndex = InModifyBones.Add(NewBone);
	InModifyBones[NewIndex].Index = NewIndex;

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

	// A dummy tail whenever no child was ADDED (excluded children count as none).
	if (InModifyBones[NewIndex].ChildIndices.IsEmpty() && Param.DummyBoneLength > 0.0f)
	{
		FKawaiiPhysicsModifyBone DummyBone;
		DummyBone.BoneRef = InModifyBones[NewIndex].BoneRef;
		DummyBone.bDummy = true;
		DummyBone.Location = InModifyBones[NewIndex].Location +
			GetForwardVector(InModifyBones[NewIndex].PoseRotation, Param.BoneForwardAxis) * Param.DummyBoneLength;
		DummyBone.PrevLocation = DummyBone.Location;
		DummyBone.PoseLocation = DummyBone.Location;
		DummyBone.Rotation = InModifyBones[NewIndex].PoseRotation;
		DummyBone.PrevRotation = DummyBone.Rotation;
		DummyBone.PoseRotation = DummyBone.Rotation;
		DummyBone.PoseScale = InModifyBones[NewIndex].PoseScale;

		const int32 DummyIndex = InModifyBones.Add(DummyBone);
		InModifyBones[DummyIndex].Index = DummyIndex;
		InModifyBones[DummyIndex].ParentIndex = NewIndex;
		InModifyBones[NewIndex].ChildIndices.Add(DummyIndex);
	}

	return NewIndex;
}

void FAnimNode_KawaiiPhysicsMerge::CalcBoneLength(FSingleNodeParameter& Param, FKawaiiPhysicsModifyBone& Bone,
                                                  const TArray<FTransform>& RefBonePose)
{
	if (!Bone.HasParent())
	{
		// A simulated root measures its reference offset from its skeleton
		// parent, and that sets the total outright.
		Bone.LengthFromRoot = 0.0f;
		if (Param.bRootBoneSimulate && RefBonePose.IsValidIndex(Bone.BoneRef.BoneIndex))
		{
			Bone.LengthFromRoot = RefBonePose[Bone.BoneRef.BoneIndex].GetLocation().Size();
			Param.TotalBoneLength = Bone.LengthFromRoot;
		}
	}
	else
	{
		const FKawaiiPhysicsModifyBone& Parent = Param.ModifyBones[Bone.ParentIndex];
		float StepLength = Param.DummyBoneLength;
		if (!Bone.bDummy && RefBonePose.IsValidIndex(Bone.BoneRef.BoneIndex))
		{
			StepLength = RefBonePose[Bone.BoneRef.BoneIndex].GetLocation().Size();
		}
		Bone.LengthFromRoot = Parent.LengthFromRoot + StepLength;
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

	// A simulated root is solved against its skeleton parent; with none, the
	// setting is cleared.
	if (Param.bRootBoneSimulate)
	{
		const int32 RootIndex = RefSkeleton.FindBoneIndex(Param.RootBone.BoneName);
		const int32 RootParent = RootIndex != INDEX_NONE ? RefSkeleton.GetParentIndex(RootIndex) : INDEX_NONE;
		if (RootParent == INDEX_NONE)
		{
			Param.bRootBoneSimulate = false;
			Param.RootParentRef = FBoneReference();
		}
		else
		{
			Param.RootParentRef.BoneName = RefSkeleton.GetBoneName(RootParent);
			Param.RootParentRef.Initialize(BoneContainer);
		}
		Param.bHasVirtualParent = false;
	}

	// NOTE: the game never resets TotalBoneLength in this node, and computes each
	// root subtree's length rates from the total accumulated so far. Reproduced.
	Param.ModifyBones.Reset();
	const TArray<FTransform>& RefBonePose = RefSkeleton.GetRefBonePose();

	auto AddRoot = [&](int32 RefIndex, const TArray<FBoneReference>& Excludes)
	{
		const int32 Start = Param.ModifyBones.Num();
		const int32 RootModify =
			AddModifyBone(Param, Param.ModifyBones, Output, BoneContainer, RefSkeleton, RefIndex, Excludes);
		if (RootModify == INDEX_NONE)
		{
			return;
		}

		CalcBoneLength(Param, Param.ModifyBones[RootModify], RefBonePose);
		for (int32 Index = Start; Index < Param.ModifyBones.Num(); ++Index)
		{
			FKawaiiPhysicsModifyBone& Bone = Param.ModifyBones[Index];
			if (Bone.LengthFromRoot > 0.0f && Param.TotalBoneLength > 0.0f)
			{
				Bone.LengthRateFromRoot = Bone.LengthFromRoot / Param.TotalBoneLength;
			}
		}
	};

	const int32 RootBoneIndex = RefSkeleton.FindBoneIndex(Param.RootBone.BoneName);
	if (RootBoneIndex != INDEX_NONE)
	{
		AddRoot(RootBoneIndex, Param.ExcludeBones);
	}

	for (const FKawaiiPhysicsRootBoneSetting& Additional : Param.AdditionalRootBones)
	{
		const int32 AdditionalIndex = RefSkeleton.FindBoneIndex(Additional.RootBone.BoneName);
		if (AdditionalIndex != INDEX_NONE)
		{
			AddRoot(AdditionalIndex, Additional.bUseOverrideExcludeBones
				                         ? Additional.OverrideExcludeBones
				                         : Param.ExcludeBones);
		}
	}

	Param.bInitialized = true;
}

///////////////////////////////////////////////////////////////////////////////
// Per-frame
///////////////////////////////////////////////////////////////////////////////

void FAnimNode_KawaiiPhysicsMerge::UpdatePhysicsSettingsOfModifyBones(FSingleNodeParameter& Param)
{
	// Game 0x7688860: the chain's values times the node's curves; LimitLinear and
	// Gravity curves only when the chain has length. No bone-scale factor here.
	const FKawaiiPhysicsSettings& Shared = Param.PhysicsSettings;
	const bool bHasLength = Param.TotalBoneLength > 0.0f;

	for (FKawaiiPhysicsModifyBone& Bone : Param.ModifyBones)
	{
		const float Rate = Bone.LengthRateFromRoot;
		FKawaiiPhysicsSettings& Out = Bone.PhysicsSettings;

		Out = Shared;
		Out.Damping = FMath::Clamp(EvalCurveMultiply(DampingCurveData, Rate, Shared.Damping), 0.0f, 1.0f);
		Out.WorldDampingLocation = FMath::Clamp(
			EvalCurveMultiply(WorldDampingLocationCurveData, Rate, Shared.WorldDampingLocation), 0.0f, 1.0f);
		Out.WorldDampingRotation = FMath::Clamp(
			EvalCurveMultiply(WorldDampingRotationCurveData, Rate, Shared.WorldDampingRotation), 0.0f, 1.0f);
		Out.Stiffness = FMath::Clamp(EvalCurveMultiply(StiffnessCurveData, Rate, Shared.Stiffness), 0.0f, 1.0f);
		Out.Radius = FMath::Max(0.0f, EvalCurveMultiply(RadiusCurveData, Rate, Shared.Radius));

		for (const FBoneShape& Shape : Param.CustomShapes)
		{
			if (Shape.ShapeType == EBoneShapeType::Capusle && Shape.DrivingBone.BoneName == Bone.BoneRef.BoneName)
			{
				Bone.ShapeHalfHeight = Shape.HalfHeight;
				Bone.OffsetRotation = Shape.OffsetRotation.Quaternion();
				break;
			}
		}

		Out.LimitAngle = FMath::Max(0.0f, EvalCurveMultiply(LimitAngleCurveData, Rate, Shared.LimitAngle));
		Out.LimitLinear = FMath::Max(0.0f, bHasLength
			                                   ? EvalCurveMultiply(LimitLinearCurveData, Rate, Shared.LimitLinear)
			                                   : Shared.LimitLinear);
		Out.GravityScale = FMath::Max(0.0f, bHasLength
			                                    ? EvalCurveMultiply(GravityCurveData, Rate, Shared.GravityScale)
			                                    : Shared.GravityScale);
	}
}

void FAnimNode_KawaiiPhysicsMerge::UpdateModifyBonesPoseTransform(FSingleNodeParameter& Param,
                                                                  FComponentSpacePoseContext& Output,
                                                                  const FBoneContainer& BoneContainer)
{
	// As the main node's (the game's 0x7686D20 was only skimmed; it has the same
	// virtual-parent, follow-target and shape-rotation updates).
	if (Param.bRootBoneSimulate)
	{
		const FCompactPoseBoneIndex ParentCompact = Param.RootParentRef.GetCompactPoseIndex(BoneContainer);
		if (Param.RootParentRef.IsValidToEvaluate(BoneContainer) && ParentCompact >= 0)
		{
			const FTransform ParentCS = Output.Pose.GetComponentSpaceTransform(ParentCompact);
			FKawaiiPhysicsModifyBone& Virtual = Param.VirtualParentBone;
			Virtual.PoseLocation = ParentCS.GetLocation();
			Virtual.Location = Virtual.PoseLocation;
			Virtual.PoseRotation = ParentCS.GetRotation();
			Virtual.Rotation = Virtual.PoseRotation;
			Virtual.PoseScale = ParentCS.GetScale3D();
			Param.bHasVirtualParent = true;
		}
	}

	for (FKawaiiPhysicsModifyBone& Bone : Param.ModifyBones)
	{
		if (!Bone.bDummy)
		{
			Bone.UpdatePoseTransform(BoneContainer, Output.Pose, ResetBoneTransformWhenBoneNotFound);

			if (Bone.bFixed && Bone.bHasFollowBone)
			{
				Param.FollowBoneRef.Initialize(BoneContainer);
				const FCompactPoseBoneIndex FollowCompact = Param.FollowBoneRef.GetCompactPoseIndex(BoneContainer);
				if (Param.FollowBoneRef.IsValidToEvaluate(BoneContainer) && FollowCompact >= 0)
				{
					Param.FollowTargetTransform =
						Param.FixedRefRelToFollow * Output.Pose.GetComponentSpaceTransform(FollowCompact);
				}
			}

			if (Bone.ShapeHalfHeight > 1e-4f && !Bone.bBoneMissing)
			{
				Bone.ShapeRotation = Bone.PoseRotation * Bone.OffsetRotation;
			}
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
	// Game 0x7689630: node-level here (not per chain); distance always tested,
	// rotation when >= 0; ends by taking this frame as the reference.
	SkelCompMoveVector = ComponentTransform.InverseTransformPosition(PreSkelCompTransform.GetLocation());
	SkelCompMoveRotation = ComponentTransform.InverseTransformRotation(PreSkelCompTransform.GetRotation());

	if (SkelCompMoveVector.SizeSquared() > FMath::Square(TeleportDistanceThreshold))
	{
		SkelCompMoveVector = FVector::ZeroVector;
	}

	const float MoveAngle = 2.0f * FMath::Acos(FMath::Clamp(static_cast<float>(SkelCompMoveRotation.W), -1.0f, 1.0f));
	if (TeleportRotationThreshold >= 0.0f && FMath::RadiansToDegrees(MoveAngle) > TeleportRotationThreshold)
	{
		SkelCompMoveRotation = FQuat::Identity;
	}

	PreSkelCompTransform = ComponentTransform;
}

const FKawaiiPhysicsModifyBone* FAnimNode_KawaiiPhysicsMerge::GetSimParent(const FSingleNodeParameter& Param,
                                                                           const FKawaiiPhysicsModifyBone& Bone)
{
	if (Param.ModifyBones.IsValidIndex(Bone.ParentIndex))
	{
		return &Param.ModifyBones[Bone.ParentIndex];
	}
	if (!Bone.HasParent() && Param.bRootBoneSimulate && Param.bHasVirtualParent)
	{
		return &Param.VirtualParentBone;
	}
	return nullptr;
}

void FAnimNode_KawaiiPhysicsMerge::SimulatePass(FComponentSpacePoseContext& Output,
                                                const FTransform& ComponentTransform, int32 LODLevel)
{
	for (FSingleNodeParameter& Param : Nodes)
	{
		if ((LODLevel < 0 || Param.IsEnabledForLOD(LODLevel)) && !Param.ModifyBones.IsEmpty())
		{
			SimulateModifyBones(Param, Output, ComponentTransform);
		}
	}

	DeltaTimeOld = DeltaTime;
}

void FAnimNode_KawaiiPhysicsMerge::SimulateModifyBones(FSingleNodeParameter& Param,
                                                       FComponentSpacePoseContext& Output,
                                                       const FTransform& ComponentTransform)
{
	// The game's SimulateModifyBones for this node (0x767E420) and its per-bone
	// step (0x767AE60): the main node's, minus wave animation, custom forces,
	// force Pre/PostApply, bone constraints, the relative-rotation limit and the
	// spring restore.
	if (DeltaTime <= 0.0f || Param.ModifyBones.IsEmpty())
	{
		return;
	}

	TArray<FKawaiiPhysicsModifyBone>& Bones = Param.ModifyBones;

	// Pre-pass.
	bool bHasFixed = false;
	for (FKawaiiPhysicsModifyBone& Bone : Bones)
	{
		Bone.bSkipSimulate = true;
		if (Bone.bBoneMissing && !Bone.bDummy)
		{
			continue;
		}
		if (!Bone.HasParent() && !Param.bRootBoneSimulate)
		{
			Bone.PrevLocation = Bone.Location;
			Bone.Location = Bone.PoseLocation;
			continue;
		}
		if (Bone.bFixed)
		{
			Bone.PrevLocation = Bone.Location;
			Bone.Location = Bone.PoseLocation;
			bHasFixed = true;
			continue;
		}
		Bone.bSkipSimulate = false;
	}
	const bool bFixTail = Param.bShouldFixTailBone && bHasFixed;

	const float Exponent = TargetFramerate * DeltaTime;
	const FVector GravityCS = ComponentTransform.InverseTransformVector(Gravity);
	const FSceneInterface* WindScene =
		(bEnableWind && ParentSkelComp) ? ParentSkelComp->GetScene() : nullptr;

	for (FKawaiiPhysicsModifyBone& Bone : Bones)
	{
		if (!Bone.IsSimulated())
		{
			continue;
		}

		const FKawaiiPhysicsSettings& Settings = Bone.PhysicsSettings;

		const FVector FollowTarget = Bone.Location + SkelCompMoveVector +
			(SkelCompMoveRotation.RotateVector(Bone.PrevLocation) - Bone.PrevLocation);

		FVector Velocity = DeltaTimeOld > 0.0f ? (Bone.Location - Bone.PrevLocation) / DeltaTimeOld : FVector::ZeroVector;
		Bone.PrevLocation = Bone.Location;
		Velocity *= 1.0f - Settings.Damping;

		if (WindScene)
		{
			FVector WindDirection = FVector::ZeroVector;
			float WindSpeed = 0.0f;
			float WindMinGust = 0.0f;
			float WindMaxGust = 0.0f;
			WindScene->GetWindParameters_GameThread(ComponentTransform.TransformPosition(Bone.PoseLocation),
			                                        WindDirection, WindSpeed, WindMinGust, WindMaxGust);

			const FVector WindCS = ComponentTransform.InverseTransformVector(WindDirection) * WindSpeed * WindScale;
			Velocity += WindCS * FMath::FRandRange(0.0f, 2.0f) * TargetFramerate;
		}

		Bone.Location += Velocity * DeltaTime;
		Bone.Location += SkelCompMoveVector * (1.0f - Settings.WorldDampingLocation);
		Bone.Location += (SkelCompMoveRotation.RotateVector(Bone.PrevLocation) - Bone.PrevLocation) *
			(1.0f - Settings.WorldDampingRotation);

		// LimitLinear: the soft tanh clamp around the rigid-follow target.
		if (Settings.LimitLinear > 0.0f && !Bone.Location.ContainsNaN() && !FollowTarget.ContainsNaN())
		{
			const FVector Offset = Bone.Location - FollowTarget;
			const float Distance = Offset.Size();
			if (Distance > 1e-8f)
			{
				const float E2R = FMath::Exp(2.0f * FMath::Min(Distance / Settings.LimitLinear, 3.0f));
				Bone.Location = FollowTarget + Offset / Distance * ((E2R - 1.0f) / (E2R + 1.0f) * Settings.LimitLinear);
			}
		}

		Bone.Location += 0.5f * GravityCS * Settings.GravityScale * DeltaTime * DeltaTime;

		// External forces (game 0x767B626): the Merge-specific apply. No PreApply
		// ever runs in this node, so each force uses the values it was loaded with.
		for (FInstancedStruct& ForceStruct : ExternalForces)
		{
			FKawaiiPhysics_ExternalForce* Force = ForceStruct.GetMutablePtr<FKawaiiPhysics_ExternalForce>();
			if (!Force || !Force->bIsEnabled)
			{
				continue;
			}

			FTransform BoneTM = FTransform::Identity;
			if (Force->ExternalForceSpace == EExternalForceSpace::BoneSpace)
			{
				const FKawaiiPhysicsModifyBone* TransformBone = &Bone;
				if (Bone.bDummy)
				{
					if (!Bones.IsValidIndex(Bone.ParentIndex))
					{
						continue;
					}
					TransformBone = &Bones[Bone.ParentIndex];
				}
				const FBoneContainer& BoneContainer = Output.Pose.GetPose().GetBoneContainer();
				const FCompactPoseBoneIndex Compact = TransformBone->BoneRef.GetCompactPoseIndex(BoneContainer);
				if (Compact >= 0)
				{
					BoneTM = Output.Pose.GetComponentSpaceTransform(Compact);
				}
			}
			Force->ApplyMerge(Bone, DeltaTime, BoneTM);
		}

		if (bFixTail)
		{
			continue;
		}

		FVector BaseLocation = Bone.PoseLocation;
		if (Bones.IsValidIndex(Bone.ParentIndex))
		{
			const FKawaiiPhysicsModifyBone& ParentBone = Bones[Bone.ParentIndex];
			BaseLocation = ParentBone.Location + (Bone.PoseLocation - ParentBone.PoseLocation);
		}
		Bone.Location += (BaseLocation - Bone.Location) * AdjustRateByExponent(Settings.Stiffness, Exponent);
	}

	// Fix-tail re-solve. The Merge node's own blocks (0x767E859 / 0x767F8ED) were
	// located but not decoded; the main node's decoded algorithm is used.
	const FKawaiiPhysicsModifyBone* Virtual =
		Param.bRootBoneSimulate && Param.bHasVirtualParent ? &Param.VirtualParentBone : nullptr;
	if (bFixTail)
	{
		KawaiiPhysics::FixTailStiffnessSweep(Bones, Param.TotalBoneLength,
		                                     Param.FollowTargetTransform.GetLocation(), Exponent);
	}

	for (FKawaiiPhysicsModifyBone& Bone : Bones)
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
		// NOT PORTED: world collision (game 0x766E060, not decoded).
	}

	// The game only clears the constraint multipliers here; it never solves the
	// Merge node's bone constraints (0x767F695).
	if (BoneConstraintIterationCountAfterCollision > 0)
	{
		for (FModifyBoneConstraint& Constraint : MergedBoneConstraints)
		{
			Constraint.Lambda = 0.0f;
		}
	}

	for (FKawaiiPhysicsModifyBone& Bone : Bones)
	{
		if (!Bone.IsSimulated())
		{
			continue;
		}

		const FKawaiiPhysicsModifyBone* Parent = GetSimParent(Param, Bone);
		if (!Parent)
		{
			continue;
		}

		KawaiiPhysics::AdjustByAngleLimit(Bone, *Parent);
		KawaiiPhysics::AdjustByPlanarConstraint(Bone, *Parent, PlanarConstraint);

		if (!bFixTail)
		{
			const float BoneLength = (Bone.PoseLocation - Parent->PoseLocation).Size();
			Bone.Location = Parent->Location + (Bone.Location - Parent->Location).GetSafeNormal() * BoneLength;
		}
	}

	if (bFixTail)
	{
		KawaiiPhysics::FixTailLengthSweep(Bones, Param.TotalBoneLength, Virtual);
	}
}

void FAnimNode_KawaiiPhysicsMerge::ApplySimulateResult(FSingleNodeParameter& Param,
                                                       FComponentSpacePoseContext& Output,
                                                       const FBoneContainer& BoneContainer,
                                                       TArray<FBoneTransform>& OutBoneTransforms)
{
	// Game 0x7672C70: the same rule as the main node's ApplySimulateResult.
	for (FKawaiiPhysicsModifyBone& Bone : Param.ModifyBones)
	{
		Bone.PrevRotation = Bone.Rotation;
		Bone.Rotation = Bone.PoseRotation;
		Bone.bKeepPoseLocation = false;
	}

	for (int32 Index = Param.bRootBoneSimulate ? 0 : 1; Index < Param.ModifyBones.Num(); ++Index)
	{
		FKawaiiPhysicsModifyBone& Bone = Param.ModifyBones[Index];
		FKawaiiPhysicsModifyBone* ParentBone = const_cast<FKawaiiPhysicsModifyBone*>(GetSimParent(Param, Bone));
		const bool bRealParent = ParentBone && ParentBone != &Param.VirtualParentBone && !ParentBone->bBoneMissing;

		if (bRealParent && ParentBone->ChildIndices.Num() <= 1)
		{
			const FVector PoseVector = Bone.PoseLocation - ParentBone->PoseLocation;
			const FVector SimulateVector = Bone.Location - ParentBone->Location;
			if (PoseVector.GetSafeNormal() == SimulateVector.GetSafeNormal())
			{
				Bone.bKeepPoseLocation = true;
				continue;
			}

			ParentBone->Rotation = KawaiiPhysics::GameFindBetweenVectors(PoseVector, SimulateVector) *
				ParentBone->PoseRotation;
		}

		if (Bone.bFixed && Bone.bHasFollowBone && !Bone.bDummy)
		{
			Bone.Rotation = Param.FollowTargetTransform.GetRotation();
		}
	}

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
		NewTransform.SetLocation(Bone.bKeepPoseLocation ? Bone.PoseLocation : Bone.Location);
		NewTransform.SetRotation(Bone.Rotation);

		OutBoneTransforms.Add(FBoneTransform(CompactPoseIndex, NewTransform));
	}
}

void FAnimNode_KawaiiPhysicsMerge::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output,
                                                                     TArray<FBoneTransform>& OutBoneTransforms)
{
	// The game's order (0x7674F60).
	if (DeltaTime <= 0.0f)
	{
		return;   // no output at all; the incoming pose passes through
	}

	const FBoneContainer& BoneContainer = Output.Pose.GetPose().GetBoneContainer();
	FTransform ComponentTransform = Output.AnimInstanceProxy->GetComponentTransform();

#if WITH_EDITORONLY_DATA
	LastEvaluatedTime = FPlatformTime::Seconds();
#endif

	if (bResetDynamics)
	{
		for (FSingleNodeParameter& Param : Nodes)
		{
			Param.ModifyBones.Empty();
		}
		bResetDynamics = false;
		bInitPhysicsSettings = false;
	}

	// Any LOD-enabled chain with an invalid root (or additional root) stops the
	// whole node for this frame.
	for (const FSingleNodeParameter& Param : Nodes)
	{
		if (!Param.IsEnabledForLOD(CachedLODLevel))
		{
			continue;
		}
		if (!Param.RootBone.IsValidToEvaluate(BoneContainer) ||
			Param.AdditionalRootBones.ContainsByPredicate([&BoneContainer](const FKawaiiPhysicsRootBoneSetting& Additional)
			{
				return !Additional.RootBone.IsValidToEvaluate(BoneContainer);
			}))
		{
			return;
		}
	}

	for (FSingleNodeParameter& Param : Nodes)
	{
		if (!Param.IsEnabledForLOD(CachedLODLevel) || (Param.bInitialized && !Param.ModifyBones.IsEmpty()))
		{
			continue;
		}

		InitModifyBones(Param, Output, BoneContainer);
		if (!Param.ModifyBones.IsEmpty())
		{
			bInitPhysicsSettings = false;
		}
		PreSkelCompTransform = ComponentTransform;
	}

	if (!bInitPhysicsSettings || bUpdatePhysicsSettingsInGame)
	{
		for (FSingleNodeParameter& Param : Nodes)
		{
			UpdatePhysicsSettingsOfModifyBones(Param);
		}
		bInitPhysicsSettings = true;
	}

	// One limit resolution shared by every chain (always, unlike the main node).
	// No bUseParentCompBone path and no bone scale in this node.
	KawaiiPhysics::ResolveAllLimits(SphericalLimits, CapsuleLimits, BoxLimits, PlanarLimits,
	                                Output, BoneContainer, false);
	KawaiiPhysics::ResolveAllLimits(SphericalLimitsData, CapsuleLimitsData, BoxLimitsData, PlanarLimitsData,
	                                Output, BoneContainer, false);

	for (FSingleNodeParameter& Param : Nodes)
	{
		if (Param.IsEnabledForLOD(CachedLODLevel) && !Param.ModifyBones.IsEmpty())
		{
			UpdateModifyBonesPoseTransform(Param, Output, BoneContainer);
		}
	}

	UpdateSkelCompMove(ComponentTransform);

	if (bNeedWarmUp && WarmUpFrames > 0)
	{
		for (int32 Frame = 0; Frame < WarmUpFrames; ++Frame)
		{
			SimulatePass(Output, ComponentTransform, INDEX_NONE);
		}
		bNeedWarmUp = false;
	}
	SimulatePass(Output, ComponentTransform, CachedLODLevel);

	for (FSingleNodeParameter& Param : Nodes)
	{
		if (Param.IsEnabledForLOD(CachedLODLevel) && !Param.ModifyBones.IsEmpty())
		{
			ApplySimulateResult(Param, Output, BoneContainer, OutBoneTransforms);
		}
	}

	OutBoneTransforms.Sort(FCompareBoneTransformIndex());
}
