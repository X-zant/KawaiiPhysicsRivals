// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License
//
// Setup and simulation. The collision and constraint solvers live in
// AnimNode_KawaiiPhysicsCollision.cpp.

#include "AnimNode_KawaiiPhysics.h"

#include "KawaiiPhysics.h"
#include "KawaiiPhysicsCustomExternalForce.h"
#include "KawaiiPhysicsExternalForce.h"
#include "KawaiiPhysicsLimitsDataAsset.h"
#include "KawaiiPhysicsBoneConstraintsDataAsset.h"
#include "KawaiiPhysicsSolvers.h"

#include "Animation/AnimInstanceProxy.h"
#include "Components/SkeletalMeshComponent.h"
// USkeletalBodySetup lives in PhysicsAsset.h in 5.3; there is no separate header.
#include "PhysicsEngine/PhysicsAsset.h"
#include "SceneInterface.h"

#if ENABLE_ANIM_DEBUG
TAutoConsoleVariable<bool> CVarAnimNodeKawaiiPhysicsEnable(
	TEXT("a.AnimNode.KawaiiPhysics.Enable"), true, TEXT("Enables or disables Kawaii Physics."));
TAutoConsoleVariable<bool> CVarAnimNodeKawaiiPhysicsDebug(
	TEXT("a.AnimNode.KawaiiPhysics.Debug"), false, TEXT("Draws Kawaii Physics debug info."));
TAutoConsoleVariable<bool> CVarAnimNodeKawaiiPhysicsDebugLengthRate(
	TEXT("a.AnimNode.KawaiiPhysics.Debug.LengthRate"), false, TEXT("Draws each bone's length rate from the root."));
#endif

using KawaiiPhysics::AdjustRateByExponent;

FAnimNode_KawaiiPhysics::FAnimNode_KawaiiPhysics()
{
	// One empty chain, so a freshly placed node has something to fill in.
	Chains.AddDefaulted();
}

void FAnimNode_KawaiiPhysics::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	Super::Initialize_AnyThread(Context);

	for (FKawaiiPhysicsChain& Chain : Chains)
	{
		Chain.bInitialized = false;
		Chain.ModifyBones.Empty();

		// The game (0x7677935-0x76779E8) runs Initialize on temporary COPIES of
		// the forces, so the live forces are never initialized here -- notably a
		// Curve force's MaxCurveTime stays as loaded (default 0: no looping).
	}

	bInitPhysicsSettings = false;
	bResetDynamics = false;

	// Game 0x76777A8: the first step's velocity divides by one target frame, not 0.
	DeltaTimeOld = TargetFramerate > 0 ? 1.0f / TargetFramerate : 0.0f;
}

void FAnimNode_KawaiiPhysics::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	// The base class re-runs InitializeBoneReferences, which re-resolves every
	// simulated bone in place. As the game does (0x7674A04), a new bone container
	// does NOT rebuild the chains, so a LOD switch keeps the simulation state.
	Super::CacheBones_AnyThread(Context);
}

void FAnimNode_KawaiiPhysics::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
	for (FKawaiiPhysicsChain& Chain : Chains)
	{
		FBoneSettings& Bones = Chain.BoneSettings;

		for (FKawaiiPhysicsModifyBone& ModifyBone : Chain.ModifyBones)
		{
			ModifyBone.BoneRef.Initialize(RequiredBones);
		}

		Bones.RootBone.Initialize(RequiredBones);
		Bones.FixedBone.Initialize(RequiredBones);
		Bones.FollowBone.Initialize(RequiredBones);

		for (FBoneReference& Bone : Bones.ExcludeBones)
		{
			Bone.Initialize(RequiredBones);
		}

		for (FKawaiiPhysicsRootBoneSetting& Additional : Bones.AdditionalRootBones)
		{
			Additional.RootBone.Initialize(RequiredBones);
			for (FBoneReference& Bone : Additional.OverrideExcludeBones)
			{
				Bone.Initialize(RequiredBones);
			}
		}

		Chain.WaveAnimSettings.WaveBeginBone.Initialize(RequiredBones);
		Chain.FollowBoneRef.Initialize(RequiredBones);
		if (Chain.BoneSettings.bRootBoneSimulate)
		{
			Chain.RootParentRef.Initialize(RequiredBones);
		}

		for (FBoneShape& Shape : Chain.PhysicsSettings.CustomShapes)
		{
			Shape.DrivingBone.Initialize(RequiredBones);
		}

		for (FModifyBoneConstraint& Constraint : Chain.BoneConstraintSettings.BoneConstraints)
		{
			Constraint.InitializeBone(RequiredBones);
		}

		ApplyBoneConstraintDataAsset(Chain, RequiredBones);

		for (FInstancedStruct& ForceStruct : Chain.ExternalForceSettings.ExternalForces)
		{
			if (FKawaiiPhysics_ExternalForce* Force = ForceStruct.GetMutablePtr<FKawaiiPhysics_ExternalForce>())
			{
				for (FBoneReference& Bone : Force->ApplyBoneFilter)
				{
					Bone.Initialize(RequiredBones);
				}
				for (FBoneReference& Bone : Force->IgnoreBoneFilter)
				{
					Bone.Initialize(RequiredBones);
				}
			}
		}
	}

	for (FBoneReference& Bone : IgnoreBones)
	{
		Bone.Initialize(RequiredBones);
	}

	ApplyLimitsDataAsset(RequiredBones);
	ApplyPhysicsAsset(RequiredBones);

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

bool FAnimNode_KawaiiPhysics::IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
#if ENABLE_ANIM_DEBUG
	if (!CVarAnimNodeKawaiiPhysicsEnable.GetValueOnAnyThread())
	{
		return false;
	}
#endif

	// Game 0x7677E20: any chain whose root and every additional root are valid.
	for (const FKawaiiPhysicsChain& Chain : Chains)
	{
		if (!Chain.BoneSettings.RootBone.IsValidToEvaluate(RequiredBones))
		{
			continue;
		}

		const bool bAdditionalValid = !Chain.BoneSettings.AdditionalRootBones.ContainsByPredicate(
			[&RequiredBones](const FKawaiiPhysicsRootBoneSetting& Additional)
			{
				return !Additional.RootBone.IsValidToEvaluate(RequiredBones);
			});
		if (bAdditionalValid)
		{
			return true;
		}
	}

	return false;
}

bool FAnimNode_KawaiiPhysics::HasPreUpdate() const
{
	return true;
}

void FAnimNode_KawaiiPhysics::PreUpdate(const UAnimInstance* InAnimInstance)
{
	// The evaluation runs on a worker thread and must not touch the component,
	// so everything we need from the game thread is cached here.
	ParentSkelComp = InAnimInstance ? InAnimInstance->GetSkelMeshComponent() : nullptr;

	if (!ParentSkelComp)
	{
		return;
	}

	CachedLODLevel = ParentSkelComp->GetPredictedLODLevel();

	if (const USceneComponent* AttachParent = ParentSkelComp->GetAttachParent())
	{
		ParentCompTransform = AttachParent->GetComponentTransform();
	}
	else
	{
		ParentCompTransform = ParentSkelComp->GetComponentTransform();
	}

	// bUseParentCompBone limits (game 0x7689A40 and siblings): the named bone
	// on the first skeletal mesh up the attachment chain, expressed in this
	// component's space. Resolved here on the game thread; a limit that cannot
	// resolve is disabled for the frame.
	const USkeletalMeshComponent* ParentMesh = nullptr;
	for (const USceneComponent* Walk = ParentSkelComp->GetAttachParent(); Walk && !ParentMesh;
	     Walk = Walk->GetAttachParent())
	{
		ParentMesh = Cast<USkeletalMeshComponent>(Walk);
	}
	auto CacheParentPlacement = [this, ParentMesh](auto& Limits)
	{
		for (auto& Limit : Limits)
		{
			Limit.bParentBonePlacementValid = false;
			if (!Limit.bUseParentCompBone || !ParentMesh || ParentMesh->GetBoneIndex(Limit.ParentDrivingBoneName) ==
				INDEX_NONE)
			{
				continue;
			}
			const FTransform Socket = ParentMesh->GetSocketTransform(Limit.ParentDrivingBoneName, RTS_Component);
			Limit.ParentBonePlacement = (Socket * ParentMesh->GetComponentTransform())
				.GetRelativeTransform(ParentSkelComp->GetComponentTransform());
			Limit.bParentBonePlacementValid = true;
		}
	};
	CacheParentPlacement(SphericalLimits);
	CacheParentPlacement(CapsuleLimits);
	CacheParentPlacement(BoxLimits);
	CacheParentPlacement(SphericalLimitsData);
	CacheParentPlacement(CapsuleLimitsData);
	CacheParentPlacement(BoxLimitsData);

	const ENetRole OwnerRole = ParentSkelComp->GetOwner()
		                           ? ParentSkelComp->GetOwner()->GetLocalRole()
		                           : ROLE_None;
	for (FKawaiiPhysicsChain& Chain : Chains)
	{
		Chain.CachedOwnerNetRole = OwnerRole;
	}
}

void FAnimNode_KawaiiPhysics::UpdateInternal(const FAnimationUpdateContext& Context)
{
	Super::UpdateInternal(Context);

	DeltaTime = Context.GetDeltaTime();
}

void FAnimNode_KawaiiPhysics::ResetDynamics(ETeleportType InTeleportType)
{
	Super::ResetDynamics(InTeleportType);

	// As upstream: only a physics reset clears the simulation (a plain teleport
	// does not), and a reset re-arms the warm-up when asked to.
	bResetDynamics |= (InTeleportType == ETeleportType::ResetPhysics);
	if (bUseWarmUpWhenResetDynamics)
	{
		bNeedWarmUp = true;
	}
}

void FAnimNode_KawaiiPhysics::GatherDebugData(FNodeDebugData& DebugData)
{
	FString DebugLine = DebugData.GetNodeName(this);

	int32 TotalBones = 0;
	for (const FKawaiiPhysicsChain& Chain : Chains)
	{
		TotalBones += Chain.ModifyBones.Num();
	}

	DebugLine += FString::Printf(TEXT("(Chains: %d, SimulatedBones: %d)"), Chains.Num(), TotalBones);
	DebugData.AddDebugItem(DebugLine);

	ComponentPose.GatherDebugData(DebugData);
}

///////////////////////////////////////////////////////////////////////////////
// Evaluation
///////////////////////////////////////////////////////////////////////////////

void FAnimNode_KawaiiPhysics::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output,
                                                               TArray<FBoneTransform>& OutBoneTransforms)
{
	// The order below is the game's compiled EvaluateSkeletalControl_AnyThread
	// (Marvel-Win64-Shipping.exe S9.5, RVA 0x7674930), step for step.

	// 0x7674957: no time passed -> no output at all; the incoming pose passes
	// through unchanged (NOT the last simulated pose).
	if (DeltaTime <= 0.0f)
	{
		return;
	}

	const FBoneContainer& BoneContainer = Output.Pose.GetPose().GetBoneContainer();
	FTransform ComponentTransform = Output.AnimInstanceProxy->GetComponentTransform();

#if WITH_EDITORONLY_DATA
	LastEvaluatedTime = FPlatformTime::Seconds();
#endif

	// 0x76749B0: every chain switched off by its LOD threshold.
	const bool bAllChainsLODDisabled = !Chains.ContainsByPredicate(
		[this](const FKawaiiPhysicsChain& Chain) { return Chain.IsEnabledForLOD(CachedLODLevel); });

	// 0x7674A5A-0x7674B01: a physics reset EMPTIES every chain; the rebuild
	// below then starts it again from the current pose.
	if (bResetDynamics)
	{
		for (FKawaiiPhysicsChain& Chain : Chains)
		{
			Chain.ModifyBones.Empty();
		}
		bInitPhysicsSettings = false;
		bResetDynamics = false;
	}

	// 0x7674C90-0x7674D2B: every chain without bones is (re)built, and every
	// such attempt resets the component-motion reference -- also when the chain
	// stays empty (root absent), which zeroes the component-motion follow of
	// the WHOLE node every frame. The game does this; so do we.
	for (FKawaiiPhysicsChain& Chain : Chains)
	{
		if (Chain.bInitialized && !Chain.ModifyBones.IsEmpty())
		{
			continue;
		}

		InitModifyBones(Chain, Output, BoneContainer);
		if (!Chain.ModifyBones.IsEmpty())
		{
			bInitPhysicsSettings = false;
		}
		PreSkelCompTransform = ComponentTransform;
	}

	// 0x7688090: node-level, every chain.
	if (!bInitPhysicsSettings || bUpdatePhysicsSettingsInGame)
	{
		for (FKawaiiPhysicsChain& Chain : Chains)
		{
			UpdatePhysicsSettingsOfModifyBones(Chain);
		}
		bInitPhysicsSettings = true;
	}

	// Limits are shared across chains: authored, then data asset, per type.
	if (!bAllChainsLODDisabled)
	{
		UpdateSphericalLimits(SphericalLimits, Output, BoneContainer, ComponentTransform);
		UpdateSphericalLimits(SphericalLimitsData, Output, BoneContainer, ComponentTransform);
		UpdateCapsuleLimits(CapsuleLimits, Output, BoneContainer, ComponentTransform);
		UpdateCapsuleLimits(CapsuleLimitsData, Output, BoneContainer, ComponentTransform);
		UpdateBoxLimits(BoxLimits, Output, BoneContainer, ComponentTransform);
		UpdateBoxLimits(BoxLimitsData, Output, BoneContainer, ComponentTransform);
		UpdatePlanerLimits(PlanarLimits, Output, BoneContainer, ComponentTransform);
		UpdatePlanerLimits(PlanarLimitsData, Output, BoneContainer, ComponentTransform);
	}

	// 0x76859A0 (LOD-aware).
	for (FKawaiiPhysicsChain& Chain : Chains)
	{
		if (Chain.IsEnabledForLOD(CachedLODLevel) && !Chain.ModifyBones.IsEmpty())
		{
			UpdateModifyBonesPoseTransform(Chain, Output, BoneContainer);
		}
	}

	// 0x76891C0: component motion per chain, against the shared reference.
	for (FKawaiiPhysicsChain& Chain : Chains)
	{
		UpdateSkelCompMove(Chain, ComponentTransform);
	}
	PreSkelCompTransform = ComponentTransform;

	// 0x7674EAB: WarmUpFrames full passes at LOD 0 (every chain), then the
	// frame's own pass at the real LOD.
	if (bNeedWarmUp && WarmUpFrames > 0)
	{
		for (int32 Frame = 0; Frame < WarmUpFrames; ++Frame)
		{
			SimulatePass(Output, ComponentTransform, INDEX_NONE);
		}
		bNeedWarmUp = false;
	}
	SimulatePass(Output, ComponentTransform, CachedLODLevel);

	// 0x7672470.
	for (FKawaiiPhysicsChain& Chain : Chains)
	{
		if (Chain.IsEnabledForLOD(CachedLODLevel) && !Chain.ModifyBones.IsEmpty())
		{
			ApplySimulateResult(Chain, Output, BoneContainer, OutBoneTransforms);
		}
	}

	// FAnimNode_SkeletalControlBase requires strictly increasing bone indices.
	OutBoneTransforms.Sort(FCompareBoneTransformIndex());

#if ENABLE_ANIM_DEBUG
	AnimDrawDebug(Output);
#endif
}

void FAnimNode_KawaiiPhysics::SimulatePass(FComponentSpacePoseContext& Output, const FTransform& ComponentTransform,
                                           int32 LODLevel)
{
	for (FKawaiiPhysicsChain& Chain : Chains)
	{
		if ((LODLevel < 0 || Chain.IsEnabledForLOD(LODLevel)) && !Chain.ModifyBones.IsEmpty())
		{
			SimulateModifyBones(Chain, Output, ComponentTransform);
		}
	}

	// 0x767E3DF: the end of every pass, warm-up passes included.
	DeltaTimeOld = DeltaTime;
	WaveTime += DeltaTime;
}

///////////////////////////////////////////////////////////////////////////////
// Chain construction
///////////////////////////////////////////////////////////////////////////////

int32 FAnimNode_KawaiiPhysics::CollectChildBones(const FReferenceSkeleton& RefSkeleton, int32 ParentBoneIndex,
                                                 TArray<int32>& Children) const
{
	Children.Reset();

	const int32 NumBones = RefSkeleton.GetNum();
	for (int32 ChildIndex = ParentBoneIndex + 1; ChildIndex < NumBones; ++ChildIndex)
	{
		if (ParentBoneIndex == RefSkeleton.GetParentIndex(ChildIndex))
		{
			Children.Add(ChildIndex);
		}
	}

	return Children.Num();
}

int32 FAnimNode_KawaiiPhysics::AddModifyBone(FKawaiiPhysicsChain& Chain,
                                             TArray<FKawaiiPhysicsModifyBone>& InModifyBones,
                                             FComponentSpacePoseContext& Output, const FBoneContainer& BoneContainer,
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

	// Recurse into the children, then wire the parent/child indices up.
	TArray<int32> Children;
	CollectChildBones(RefSkeleton, BoneIndex, Children);

	if (Children.Num() == 0)
	{
		// End of the chain: optionally append a synthetic tail bone so that the
		// last real bone has something to orient towards.
		if (Chain.BoneSettings.DummyBoneLength > 0.0f)
		{
			FKawaiiPhysicsModifyBone DummyBone;
			DummyBone.BoneRef = InModifyBones[NewIndex].BoneRef;
			DummyBone.bDummy = true;
			DummyBone.Location = InModifyBones[NewIndex].Location +
				GetBoneForwardVector(InModifyBones[NewIndex].Rotation, Chain.BoneSettings.BoneForwardAxis) *
				Chain.BoneSettings.DummyBoneLength;
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
				AddModifyBone(Chain, InModifyBones, Output, BoneContainer, RefSkeleton, ChildBoneIndex, InExcludeBones);
			if (ChildModifyIndex != INDEX_NONE)
			{
				InModifyBones[ChildModifyIndex].ParentIndex = NewIndex;
				InModifyBones[NewIndex].ChildIndices.Add(ChildModifyIndex);
			}
		}
	}

	return NewIndex;
}

void FAnimNode_KawaiiPhysics::CalcBoneLength(FKawaiiPhysicsChain& Chain, FKawaiiPhysicsModifyBone& Bone,
                                             const TArray<FTransform>& RefBonePose)
{
	if (!Bone.HasParent())
	{
		// Game 0x76734B0: a simulated root measures its own reference offset
		// from its skeleton parent, and that sets the chain's total outright.
		Bone.LengthFromRoot = 0.0f;
		if (Chain.BoneSettings.bRootBoneSimulate && !Bone.bDummy && RefBonePose.IsValidIndex(Bone.BoneRef.BoneIndex))
		{
			Bone.LengthFromRoot = RefBonePose[Bone.BoneRef.BoneIndex].GetLocation().Size();
			Chain.TotalBoneLength = Bone.LengthFromRoot;
		}
	}
	else
	{
		// As upstream: each step is the bone's REFERENCE-pose offset from its
		// skeleton parent (a dummy tail uses DummyBoneLength), not the distance
		// in whatever pose happens to be playing at init. This is the curve
		// lookup position for every per-bone setting, so it has to match.
		const FKawaiiPhysicsModifyBone& Parent = Chain.ModifyBones[Bone.ParentIndex];
		float StepLength = Chain.BoneSettings.DummyBoneLength;
		if (!Bone.bDummy && RefBonePose.IsValidIndex(Bone.BoneRef.BoneIndex))
		{
			StepLength = RefBonePose[Bone.BoneRef.BoneIndex].GetLocation().Size();
		}
		Bone.LengthFromRoot = Parent.LengthFromRoot + StepLength;
	}

	Chain.TotalBoneLength = FMath::Max(Chain.TotalBoneLength, Bone.LengthFromRoot);

	for (const int32 ChildIndex : Bone.ChildIndices)
	{
		CalcBoneLength(Chain, Chain.ModifyBones[ChildIndex], RefBonePose);
	}
}

void FAnimNode_KawaiiPhysics::InitModifyBones(FKawaiiPhysicsChain& Chain, FComponentSpacePoseContext& Output,
                                              const FBoneContainer& BoneContainer)
{
	const FReferenceSkeleton& RefSkeleton = BoneContainer.GetReferenceSkeleton();

	Chain.ModifyBones.Reset();
	Chain.TotalBoneLength = 0.0f;

	// Game 0x7676D57: a simulated root is solved against its skeleton parent;
	// a root with no parent cannot simulate, and the setting is cleared.
	if (Chain.BoneSettings.bRootBoneSimulate)
	{
		const int32 RootIndex = RefSkeleton.FindBoneIndex(Chain.BoneSettings.RootBone.BoneName);
		const int32 RootParent = RootIndex != INDEX_NONE ? RefSkeleton.GetParentIndex(RootIndex) : INDEX_NONE;
		if (RootParent == INDEX_NONE)
		{
			Chain.BoneSettings.bRootBoneSimulate = false;
			Chain.RootParentRef = FBoneReference();
		}
		else
		{
			Chain.RootParentRef.BoneName = RefSkeleton.GetBoneName(RootParent);
			Chain.RootParentRef.Initialize(BoneContainer);
		}
		Chain.bHasVirtualParent = false;
	}

	const FBoneSettings& Bones = Chain.BoneSettings;

	const int32 RootBoneIndex = RefSkeleton.FindBoneIndex(Bones.RootBone.BoneName);
	if (RootBoneIndex != INDEX_NONE)
	{
		AddModifyBone(Chain, Chain.ModifyBones, Output, BoneContainer, RefSkeleton, RootBoneIndex, Bones.ExcludeBones);
	}

	for (const FKawaiiPhysicsRootBoneSetting& Additional : Bones.AdditionalRootBones)
	{
		const int32 AdditionalIndex = RefSkeleton.FindBoneIndex(Additional.RootBone.BoneName);
		if (AdditionalIndex == INDEX_NONE)
		{
			continue;
		}

		const TArray<FBoneReference>& Excludes = Additional.bUseOverrideExcludeBones
			                                         ? Additional.OverrideExcludeBones
			                                         : Bones.ExcludeBones;
		AddModifyBone(Chain, Chain.ModifyBones, Output, BoneContainer, RefSkeleton, AdditionalIndex, Excludes);
	}

	if (Chain.ModifyBones.IsEmpty())
	{
		Chain.bInitialized = true;
		return;
	}

	// Lengths first: every curve lookup and the wave animation key off them.
	const TArray<FTransform>& RefBonePose = RefSkeleton.GetRefBonePose();
	for (FKawaiiPhysicsModifyBone& Bone : Chain.ModifyBones)
	{
		if (!Bone.HasParent())
		{
			CalcBoneLength(Chain, Bone, RefBonePose);
		}
	}

	for (FKawaiiPhysicsModifyBone& Bone : Chain.ModifyBones)
	{
		Bone.LengthRateFromRoot = Chain.TotalBoneLength > 0.0f
			                          ? Bone.LengthFromRoot / Chain.TotalBoneLength
			                          : 0.0f;
	}

	MarkFixedAndFollowBones(Chain, RefSkeleton, BoneContainer);
	MarkWaveBones(Chain);
	InitBoneConstraints(Chain);

	Chain.bInitialized = true;
	bInitPhysicsSettings = false;
}

void FAnimNode_KawaiiPhysics::MarkFixedAndFollowBones(FKawaiiPhysicsChain& Chain, const FReferenceSkeleton& RefSkeleton,
                                                      const FBoneContainer& BoneContainer)
{
	// Game AddModifyBone 0x7668C95-0x7668D9A: the ONLY fixed bone is
	// BoneSettings::FixedBone, and only with bShouldFixTailBone and when it is a
	// leaf in the reference skeleton. A pinned root is not "fixed" -- the
	// simulate pre-pass pins it separately. The fixed bone follows FollowBone
	// when that bone exists in the skeleton.
	const FBoneSettings& Bones = Chain.BoneSettings;

	auto RefComponentSpace = [&RefSkeleton](int32 BoneIndex)
	{
		FTransform Result = FTransform::Identity;
		for (int32 Walk = BoneIndex; Walk != INDEX_NONE; Walk = RefSkeleton.GetParentIndex(Walk))
		{
			Result = Result * RefSkeleton.GetRefBonePose()[Walk];
		}
		return Result;
	};

	for (FKawaiiPhysicsModifyBone& Bone : Chain.ModifyBones)
	{
		Bone.bFixed = false;
		Bone.bHasFollowBone = false;

		if (Bone.bDummy || !Bones.bShouldFixTailBone || Bones.FixedBone.BoneName == NAME_None ||
			Bone.BoneRef.BoneName != Bones.FixedBone.BoneName)
		{
			continue;
		}

		const int32 FixedIndex = RefSkeleton.FindBoneIndex(Bone.BoneRef.BoneName);
		bool bHasRefChild = false;
		for (int32 Child = FixedIndex + 1; Child < RefSkeleton.GetNum() && !bHasRefChild; ++Child)
		{
			bHasRefChild = RefSkeleton.GetParentIndex(Child) == FixedIndex;
		}
		if (FixedIndex == INDEX_NONE || bHasRefChild)
		{
			continue;
		}

		Bone.bFixed = true;
		Chain.FollowBoneRef = Bones.FollowBone;
		Chain.FollowBoneRef.Initialize(BoneContainer);

		const int32 FollowIndex = RefSkeleton.FindBoneIndex(Bones.FollowBone.BoneName);
		if (FollowIndex != INDEX_NONE)
		{
			Bone.bHasFollowBone = true;
			Chain.FixedRefRelToFollow = RefComponentSpace(FixedIndex).GetRelativeTransform(RefComponentSpace(FollowIndex));
		}
	}
}

void FAnimNode_KawaiiPhysics::MarkWaveBones(FKawaiiPhysicsChain& Chain)
{
	const FWaveAnimSettings& Wave = Chain.WaveAnimSettings;

	Chain.WaveBeginBoneLengthFromRoot = 0.0f;

	if (!Wave.bEnableWaveAnim)
	{
		for (FKawaiiPhysicsModifyBone& Bone : Chain.ModifyBones)
		{
			Bone.bChildOfWaveBeginBone = false;
			Bone.WaveAmplitude = 0.0f;
		}
		return;
	}

	// No begin bone means the whole chain waves.
	int32 BeginIndex = INDEX_NONE;
	if (Wave.WaveBeginBone.BoneName != NAME_None)
	{
		BeginIndex = Chain.ModifyBones.IndexOfByPredicate(
			[&Wave](const FKawaiiPhysicsModifyBone& Bone)
			{
				return !Bone.bDummy && Bone.BoneRef.BoneName == Wave.WaveBeginBone.BoneName;
			});
	}

	if (BeginIndex != INDEX_NONE)
	{
		Chain.WaveBeginBoneLengthFromRoot = Chain.ModifyBones[BeginIndex].LengthFromRoot;
	}

	for (FKawaiiPhysicsModifyBone& Bone : Chain.ModifyBones)
	{
		bool bAffected = (BeginIndex == INDEX_NONE);

		// Walk up to see whether the begin bone is an ancestor of this one.
		for (int32 Walk = Bone.Index; Walk != INDEX_NONE && !bAffected; Walk = Chain.ModifyBones[Walk].ParentIndex)
		{
			if (Walk == BeginIndex)
			{
				bAffected = true;
			}
		}

		Bone.bChildOfWaveBeginBone = bAffected;

		// The per-bone amplitude is resolved with the other settings
		// (UpdatePhysicsSettingsOfModifyBones), every settings update, as the game does.
		Bone.WaveAmplitude = 0.0f;
	}
}

///////////////////////////////////////////////////////////////////////////////
// Per-frame parameter resolution
///////////////////////////////////////////////////////////////////////////////

void FAnimNode_KawaiiPhysics::UpdatePhysicsSettingsOfModifyBones(FKawaiiPhysicsChain& Chain)
{
	const FBoneChainPhysicsSettings& Settings = Chain.PhysicsSettings;
	const FKawaiiPhysicsSettings& Shared = Settings.PhysicsSettings;

	// Curve lookup that leaves the value untouched when the curve is empty,
	// so an unauthored curve is not the same as a curve that reads zero.
	auto EvalCurve = [](const FRuntimeFloatCurve& CurveData, float Time, float Value)
	{
		const FRichCurve* Curve = CurveData.GetRichCurveConst();
		return (Curve && Curve->GetNumKeys() > 0) ? Value * Curve->Eval(Time) : Value;
	};

	// The game's rule (0x7688090):
	//  - bUseCurve: the chain value, times its curve at LengthRateFromRoot when
	//    the curve has keys (LimitLinear and Gravity only when TotalBoneLength > 0);
	//  - otherwise PhysicsParamsPerBone[i] supplies EVERY value -- the
	//    bNeedEdit* toggles are never read -- and a bone past the array's end gets
	//    FBonePhysicsParams() defaults. The chain's own values are not used.
	const FWaveAnimSettings& Wave = Chain.WaveAnimSettings;
	const FRichCurve* WaveCurve = Wave.WaveAmplitudeCurveData.GetRichCurveConst();
	const bool bWaveCurve = WaveCurve && WaveCurve->GetNumKeys() > 0;
	const float Begin = Chain.WaveBeginBoneLengthFromRoot;
	const float Total = Chain.TotalBoneLength;
	const FBonePhysicsParams DefaultParams;

	for (int32 Index = 0; Index < Chain.ModifyBones.Num(); ++Index)
	{
		FKawaiiPhysicsModifyBone& Bone = Chain.ModifyBones[Index];
		const float Rate = Bone.LengthRateFromRoot;

		FKawaiiPhysicsSettings& Out = Bone.PhysicsSettings;
		Out = Shared;

		if (Settings.bUseCurve)
		{
			Out.Damping = EvalCurve(Settings.DampingCurveData, Rate, Shared.Damping);
			Out.Stiffness = EvalCurve(Settings.StiffnessCurveData, Rate, Shared.Stiffness);
			Out.WorldDampingLocation = EvalCurve(Settings.WorldDampingLocationCurveData, Rate,
			                                     Shared.WorldDampingLocation);
			Out.WorldDampingRotation = EvalCurve(Settings.WorldDampingRotationCurveData, Rate,
			                                     Shared.WorldDampingRotation);
			Out.Radius = EvalCurve(Settings.RadiusCurveData, Rate, Shared.Radius);
			Out.LimitAngle = EvalCurve(Settings.LimitAngleCurveData, Rate, Shared.LimitAngle);
			if (Total > 0.0f)
			{
				Out.LimitLinear = EvalCurve(Settings.LimitLinearCurveData, Rate, Shared.LimitLinear);
				Out.GravityScale = EvalCurve(Settings.GravityCurveData, Rate, Shared.GravityScale);
			}
		}
		else
		{
			const FBonePhysicsParams& Params = Settings.PhysicsParamsPerBone.IsValidIndex(Index)
				                                   ? Settings.PhysicsParamsPerBone[Index]
				                                   : DefaultParams;

			Out.Damping = Params.Damping;
			Out.Stiffness = Params.Stiffness;
			Out.WorldDampingLocation = Params.WorldDampingLocation;
			Out.WorldDampingRotation = Params.WorldDampingRotation;
			Out.Radius = Params.Radius;
			Out.LimitAngle = Params.LimitAngle;
			Out.LimitLinear = Params.LimitLinear;
			Out.GravityScale = Params.GravityScale;
		}

		Out.Damping = FMath::Clamp(Out.Damping, 0.0f, 1.0f);
		Out.Stiffness = FMath::Clamp(Out.Stiffness, 0.0f, 1.0f);
		Out.WorldDampingLocation = FMath::Clamp(Out.WorldDampingLocation, 0.0f, 1.0f);
		Out.WorldDampingRotation = FMath::Clamp(Out.WorldDampingRotation, 0.0f, 1.0f);
		Out.Radius = FMath::Max(0.0f, Out.Radius);
		Out.LimitAngle = FMath::Max(0.0f, Out.LimitAngle);
		Out.LimitLinear = FMath::Max(0.0f, Out.LimitLinear);
		Out.GravityScale = FMath::Max(0.0f, Out.GravityScale);

		// 0x768850F: the radius follows the bone's component-space scale (a dummy
		// tail carries its parent's scale already).
		Out.Radius *= Bone.PoseScale.X;

		// 0x7688532: the first CAPSULE shape driven by this bone gives it a half
		// height and an offset rotation; with no match both keep their values.
		for (const FBoneShape& Shape : Settings.CustomShapes)
		{
			if (Shape.ShapeType == EBoneShapeType::Capusle && Shape.DrivingBone.BoneName == Bone.BoneRef.BoneName)
			{
				Bone.ShapeHalfHeight = Shape.HalfHeight;
				Bone.OffsetRotation = Shape.OffsetRotation.Quaternion();
				break;
			}
		}

		// 0x76886DE: wave amplitude, recomputed on every settings update.
		if (Wave.bEnableWaveAnim)
		{
			float Amplitude = Wave.WaveAmplitude;
			if (Total > 0.0f && bWaveCurve)
			{
				Amplitude = FMath::Abs(Begin - Total) < 1e-4f
					            ? 0.0f
					            : WaveCurve->Eval(FMath::Max((Bone.LengthFromRoot - Begin) / (Total - Begin), 0.0f)) *
					            Amplitude;
			}
			Bone.WaveAmplitude = Bone.bChildOfWaveBeginBone ? FMath::Max(Amplitude, 0.0f) : 0.0f;
		}
	}
}

void FAnimNode_KawaiiPhysics::UpdateModifyBonesPoseTransform(FKawaiiPhysicsChain& Chain,
                                                             FComponentSpacePoseContext& Output,
                                                             const FBoneContainer& BoneContainer)
{
	// Game 0x7685A94: the virtual parent rides the root's skeleton parent
	// exactly (location = pose); it keeps last frame's values if that bone is
	// not in this pose.
	if (Chain.BoneSettings.bRootBoneSimulate)
	{
		const FCompactPoseBoneIndex ParentCompact = Chain.RootParentRef.GetCompactPoseIndex(BoneContainer);
		if (Chain.RootParentRef.IsValidToEvaluate(BoneContainer) && ParentCompact >= 0)
		{
			const FTransform ParentCS = Output.Pose.GetComponentSpaceTransform(ParentCompact);
			FKawaiiPhysicsModifyBone& Virtual = Chain.VirtualParentBone;
			Virtual.PoseLocation = ParentCS.GetLocation();
			Virtual.Location = Virtual.PoseLocation;
			Virtual.PoseRotation = ParentCS.GetRotation();
			Virtual.Rotation = Virtual.PoseRotation;
			Virtual.PoseScale = ParentCS.GetScale3D();
			Chain.bHasVirtualParent = true;
		}
	}

	for (FKawaiiPhysicsModifyBone& Bone : Chain.ModifyBones)
	{
		if (!Bone.bDummy)
		{
			Bone.UpdatePoseTransform(BoneContainer, Output.Pose, ResetBoneTransformWhenBoneNotFound);

			// Game 0x7685CDD: the fixed bone's follow target, re-resolved every frame.
			if (Bone.bFixed && Bone.bHasFollowBone)
			{
				Chain.FollowBoneRef.Initialize(BoneContainer);
				const FCompactPoseBoneIndex FollowCompact = Chain.FollowBoneRef.GetCompactPoseIndex(BoneContainer);
				if (Chain.FollowBoneRef.IsValidToEvaluate(BoneContainer) && FollowCompact >= 0)
				{
					Chain.FollowTargetTransform =
						Chain.FixedRefRelToFollow * Output.Pose.GetComponentSpaceTransform(FollowCompact);
				}
			}

			// Game 0x76865E5: a capsule-shaped bone takes its shape's rotation.
			if (Bone.ShapeHalfHeight > 1e-4f && !Bone.bBoneMissing)
			{
				Bone.ShapeRotation = Bone.PoseRotation * Bone.OffsetRotation;
			}
			continue;
		}

		// A dummy bone has no pose of its own; project it off its parent.
		if (Bone.HasParent())
		{
			const FKawaiiPhysicsModifyBone& Parent = Chain.ModifyBones[Bone.ParentIndex];
			Bone.PoseLocation = Parent.PoseLocation +
				GetBoneForwardVector(Parent.PoseRotation, Chain.BoneSettings.BoneForwardAxis) *
				Chain.BoneSettings.DummyBoneLength;
			Bone.PoseRotation = Parent.PoseRotation;
			Bone.PoseScale = Parent.PoseScale;
		}
	}
}

void FAnimNode_KawaiiPhysics::UpdateSkelCompMove(FKawaiiPhysicsChain& Chain, const FTransform& ComponentTransform)
{
	// Game 0x76891C0, per chain.
	Chain.SkelCompMoveVector = ComponentTransform.InverseTransformPosition(PreSkelCompTransform.GetLocation());
	Chain.SkelCompMoveRotation = ComponentTransform.InverseTransformRotation(PreSkelCompTransform.GetRotation());

	// A teleport is not motion the cloth should react to; drop it entirely.
	// Distance tested unconditionally (so 0 disables following translation
	// altogether), rotation when >= 0, angle = 2 acos(clamp(w, -1, 1)).
	const float DistanceThreshold = Chain.PhysicsSettings.TeleportDistanceThreshold;
	if (Chain.SkelCompMoveVector.SizeSquared() > FMath::Square(DistanceThreshold))
	{
		Chain.SkelCompMoveVector = FVector::ZeroVector;
	}

	const float RotationThreshold = Chain.PhysicsSettings.TeleportRotationThreshold;
	const float MoveAngle = 2.0f * FMath::Acos(FMath::Clamp(static_cast<float>(Chain.SkelCompMoveRotation.W), -1.0f, 1.0f));
	if (RotationThreshold >= 0.0f && FMath::RadiansToDegrees(MoveAngle) > RotationThreshold)
	{
		Chain.SkelCompMoveRotation = FQuat::Identity;
	}
}

///////////////////////////////////////////////////////////////////////////////
// Simulation
///////////////////////////////////////////////////////////////////////////////

void FAnimNode_KawaiiPhysics::SimulateModifyBones(FKawaiiPhysicsChain& Chain, FComponentSpacePoseContext& Output,
                                                  const FTransform& ComponentTransform)
{
	if (DeltaTime <= 0.0f || Chain.ModifyBones.IsEmpty())
	{
		return;
	}

	const FSceneInterface* Scene = Output.AnimInstanceProxy->GetSkelMeshComponent() &&
	                               Output.AnimInstanceProxy->GetSkelMeshComponent()->GetScene()
		                               ? Output.AnimInstanceProxy->GetSkelMeshComponent()->GetScene()
		                               : nullptr;

	const USkeletalMeshComponent* SkelComp = Output.AnimInstanceProxy->GetSkelMeshComponent();

	// Damping and stiffness are authored per frame at TargetFramerate.
	const float Exponent = TargetFramerate > 0 ? DeltaTime * TargetFramerate : 1.0f;

	const FVector GravityCS = ComponentTransform.InverseTransformVector(Chain.ExternalForceSettings.Gravity);

	// Pre-pass (game 0x767B9F0): decide which bones simulate. An invalid bone
	// (or a dummy whose parent is invalid) is skipped as it is; a pinned root
	// and a fixed bone ride the pose, keeping last frame's position as history.
	bool bHasFixed = false;
	for (FKawaiiPhysicsModifyBone& Bone : Chain.ModifyBones)
	{
		const bool bInvalid = Bone.bBoneMissing ||
			(Bone.bDummy && Chain.ModifyBones.IsValidIndex(Bone.ParentIndex) &&
				Chain.ModifyBones[Bone.ParentIndex].bBoneMissing);
		Bone.bSkipSimulate = true;
		if (bInvalid)
		{
			continue;
		}

		if (!Bone.HasParent() && !Chain.BoneSettings.bRootBoneSimulate)
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

	// The game runs the custom (instanced-object) forces' PreApply first.
	for (UKawaiiPhysics_CustomExternalForce* Custom : Chain.ExternalForceSettings.CustomExternalForces)
	{
		if (Custom && Custom->bIsEnabled)
		{
			Custom->PreApply(*this, SkelComp);
		}
	}

	// Game 0x767BBF0: PreApply runs for every force, enabled or not (timers
	// and the random scale advance while a force is disabled).
	for (int32 ForceIndex = 0; ForceIndex < Chain.ExternalForceSettings.ExternalForces.Num(); ++ForceIndex)
	{
		FInstancedStruct& ForceStruct = Chain.ExternalForceSettings.ExternalForces[ForceIndex];
		if (FKawaiiPhysics_ExternalForce* Force = ForceStruct.GetMutablePtr<FKawaiiPhysics_ExternalForce>())
		{
			Force->PreApply(*this, Chain, SkelComp);
		}
	}

	for (FKawaiiPhysicsModifyBone& Bone : Chain.ModifyBones)
	{
		if (Bone.IsSimulated())
		{
			Simulate(Chain, Bone, Scene, ComponentTransform, GravityCS, Exponent, SkelComp, Output, bHasFixed);
		}
	}

	// Fix-tail mode: with bShouldFixTailBone and a fixed bone the game drops the
	// per-bone stiffness pull and the regular length restore, and re-solves the
	// chain from both ends instead, blending by distance along the chain.
	const bool bFixTail = Chain.BoneSettings.bShouldFixTailBone && bHasFixed;
	if (bFixTail)
	{
		FixTailStiffness(Chain, Exponent);
	}

	// Game 0x767CD11: an index loop that re-reads Num, so after a one-shot force
	// removes itself the next one is skipped this frame.
	for (int32 ForceIndex = 0; ForceIndex < Chain.ExternalForceSettings.ExternalForces.Num(); ++ForceIndex)
	{
		FInstancedStruct& ForceStruct = Chain.ExternalForceSettings.ExternalForces[ForceIndex];
		if (FKawaiiPhysics_ExternalForce* Force = ForceStruct.GetMutablePtr<FKawaiiPhysics_ExternalForce>())
		{
			Force->PostApply(*this, Chain);
		}
	}

	for (FKawaiiPhysicsModifyBone& Bone : Chain.ModifyBones)
	{
		if (!Bone.IsSimulated())
		{
			continue;
		}

		AdjustBySphereCollision(Bone, SphericalLimits);
		AdjustBySphereCollision(Bone, SphericalLimitsData);
		AdjustByCapsuleCollision(Bone, CapsuleLimits);
		AdjustByCapsuleCollision(Bone, CapsuleLimitsData);
		AdjustByBoxCollision(Bone, BoxLimits);
		AdjustByBoxCollision(Bone, BoxLimitsData);
		AdjustByPlanerCollision(Bone, PlanarLimits);
		AdjustByPlanerCollision(Bone, PlanarLimitsData);

		if (bAllowWorldCollision)
		{
			AdjustByWorldCollision(Chain, Bone, SkelComp);
		}
	}

	// The bone constraints run ONCE, after collision. The game never reads
	// BoneConstraintIterationCountBeforeCollision at runtime (its only reader is
	// the chain copy constructor 0x764E3A0).
	AdjustByBoneConstraints(Chain, Chain.BoneConstraintSettings.BoneConstraintIterationCountAfterCollision);

	const FBoneChainPhysicsSettings& ChainPhysics = Chain.PhysicsSettings;
	for (int32 Index = 0; Index < Chain.ModifyBones.Num(); ++Index)
	{
		FKawaiiPhysicsModifyBone& Bone = Chain.ModifyBones[Index];
		Bone.bRotationLimited = false;   // 0x767CF13: cleared for every bone, before the skip test
		if (!Bone.IsSimulated())
		{
			continue;
		}

		// A simulated root solves against the chain's virtual parent.
		const FKawaiiPhysicsModifyBone* ParentPtr = GetSimParent(Chain, Bone);
		if (!ParentPtr)
		{
			continue;
		}
		const FKawaiiPhysicsModifyBone& Parent = *ParentPtr;
		AdjustByAngleLimit(Chain, Bone, Parent);
		AdjustByPlanarConstraint(Chain, Bone, Parent);
		AdjustByRelativeRotationLimit(Chain, Bone, Parent);

		// 0x767D078: fix-tail mode replaces the length restore (below the loop).
		if (bFixTail)
		{
			continue;
		}

		// Length restore (0x767D089-0x767D3E7). SpringStiffness lives HERE, as a
		// soft restore, not as a second stiffness pull. The game reads it from
		// PhysicsParamsPerBone[i + 1] -- an off-by-one against every other
		// per-bone lookup, reproduced deliberately.
		const float BoneLength = (Bone.PoseLocation - Parent.PoseLocation).Size();
		float SpringStiffness = 0.0f;
		if (!ChainPhysics.bUseCurve && ChainPhysics.PhysicsParamsPerBone.IsValidIndex(Index + 1))
		{
			SpringStiffness = ChainPhysics.PhysicsParamsPerBone[Index + 1].SpringStiffness;
		}

		const FVector Offset = Bone.Location - Parent.Location;
		if (SpringStiffness > 0.0f)
		{
			const float Current = Offset.Size();
			if (Current > 1e-7f)
			{
				Bone.Location -= Offset / Current * ((Current - BoneLength) * SpringStiffness);
			}
		}
		else
		{
			Bone.Location = Parent.Location + Offset.GetSafeNormal() * BoneLength;
		}
	}

	if (bFixTail)
	{
		FixTailLengths(Chain);
	}

	// Orientation, by the game's ApplySimulateResult (0x7672470), in its order
	// so later writes win as they do there. Done here so Rotation is final
	// with the positions. For bone i (from 0 when the root simulates, else 1):
	//  - its parent, if that has at most one child and a real bone, turns by the
	//    rotation taking the pose parent->child vector onto the simulated one,
	//    on top of the parent's pose rotation -- or takes the rotation the
	//    relative-rotation limit cached. When neither applies because the
	//    direction did not change, bone i is left entirely at its pose
	//    location and nothing else is written for it;
	//  - a fixed bone with a follow bone takes the follow target's rotation.
	// No OffsetRotation is applied. The virtual parent has no output.
	for (FKawaiiPhysicsModifyBone& Bone : Chain.ModifyBones)
	{
		Bone.PrevRotation = Bone.Rotation;
		Bone.Rotation = Bone.PoseRotation;
		Bone.bKeepPoseLocation = false;
	}

	for (int32 Index = Chain.BoneSettings.bRootBoneSimulate ? 0 : 1; Index < Chain.ModifyBones.Num(); ++Index)
	{
		FKawaiiPhysicsModifyBone& Bone = Chain.ModifyBones[Index];
		FKawaiiPhysicsModifyBone* ParentBone = const_cast<FKawaiiPhysicsModifyBone*>(GetSimParent(Chain, Bone));
		const bool bRealParent = ParentBone && ParentBone != &Chain.VirtualParentBone && !ParentBone->bBoneMissing;

		if (bRealParent && ParentBone->ChildIndices.Num() <= 1)
		{
			if (Bone.bRotationLimited)
			{
				ParentBone->Rotation = Bone.LimitedParentRotation;
			}
			else
			{
				const FVector PoseVector = Bone.PoseLocation - ParentBone->PoseLocation;
				const FVector SimulateVector = Bone.Location - ParentBone->Location;
				if (PoseVector.GetSafeNormal() == SimulateVector.GetSafeNormal())
				{
					Bone.bKeepPoseLocation = true;
					continue;
				}

				// (The game negates both vectors for a negative forward axis;
				// that leaves the result unchanged, so it is omitted.)
				ParentBone->Rotation = KawaiiPhysics::GameFindBetweenVectors(PoseVector, SimulateVector) *
					ParentBone->PoseRotation;
			}
		}

		if (Bone.bFixed && Bone.bHasFollowBone && !Bone.bDummy)
		{
			Bone.Rotation = Chain.FollowTargetTransform.GetRotation();
		}
	}
}

void FAnimNode_KawaiiPhysics::Simulate(FKawaiiPhysicsChain& Chain, FKawaiiPhysicsModifyBone& Bone,
                                       const FSceneInterface* Scene, const FTransform& ComponentTransform,
                                       const FVector& GravityCS, const float& Exponent,
                                       const USkeletalMeshComponent* SkelComp, FComponentSpacePoseContext& Output,
                                       bool bHasFixed)
{
	const FKawaiiPhysicsSettings& Settings = Bone.PhysicsSettings;

	// The game's per-bone step (0x7679F60), in its order. Only Stiffness is
	// exponent-adjusted; Damping and the two world-damping rates apply raw.

	// The rigid-follow target of the LimitLinear clamp, from the entry state.
	const FVector FollowTarget = Bone.Location + Chain.SkelCompMoveVector +
		(Chain.SkelCompMoveRotation.RotateVector(Bone.PrevLocation) - Bone.PrevLocation);

	// --- move by last frame's velocity, damped ---
	FVector Velocity = FVector::ZeroVector;
	if (DeltaTimeOld > 0.0f)
	{
		Velocity = (Bone.Location - Bone.PrevLocation) / DeltaTimeOld;
	}

	Bone.PrevLocation = Bone.Location;
	Velocity *= 1.0f - Settings.Damping;

	// Wind feeds the velocity, scaled by TargetFramerate -- not the position.
	if (Chain.ExternalForceSettings.bEnableWind && Scene)
	{
		Velocity += GetWindVelocity(Chain, Scene, ComponentTransform, Bone) * TargetFramerate;
	}

	Bone.Location += Velocity * DeltaTime;

	// --- follow the component's own motion ---
	Bone.Location += Chain.SkelCompMoveVector * (1.0f - Settings.WorldDampingLocation);
	// Rotates the (new) PREVIOUS location.
	Bone.Location += (Chain.SkelCompMoveRotation.RotateVector(Bone.PrevLocation) - Bone.PrevLocation) *
		(1.0f - Settings.WorldDampingRotation);

	// --- LimitLinear (0x767A442): a SOFT clamp around the rigid-follow target ---
	// Location = F + dir * tanh(min(|d| / Limit, 3)) * Limit, before gravity.
	const float LimitLinear = Settings.LimitLinear;
	if (LimitLinear > 0.0f && !Bone.Location.ContainsNaN() && !FollowTarget.ContainsNaN())
	{
		const FVector Offset = Bone.Location - FollowTarget;
		const float Distance = Offset.Size();
		if (Distance > 1e-8f)
		{
			// tanh via exp, as the game computes it: (e^2r - 1) / (e^2r + 1).
			const float E2R = FMath::Exp(2.0f * FMath::Min(Distance / LimitLinear, 3.0f));
			Bone.Location = FollowTarget + Offset / Distance * ((E2R - 1.0f) / (E2R + 1.0f) * LimitLinear);
		}
	}

	// --- gravity ---
	// 0.5 * g * dt^2 (the game's default path of p.KawaiiPhysics.EnableOldPhysicsMethodGravity).
	Bone.Location += 0.5f * GravityCS * Settings.GravityScale * DeltaTime * DeltaTime;

	// --- external forces, before the pull to pose, as upstream ---
	// Upstream order: custom forces first, then the preset forces. Both get the
	// bone's component-space transform (its parent's, for a dummy tail bone);
	// presets only use it in bone space.
	TOptional<FTransform> CachedBoneTM;
	auto GetBoneTM = [&]() -> const FTransform&
	{
		if (!CachedBoneTM.IsSet())
		{
			const FBoneContainer& BoneContainer = Output.Pose.GetPose().GetBoneContainer();
			const FBoneReference& TransformBone =
				(Bone.bDummy && Chain.ModifyBones.IsValidIndex(Bone.ParentIndex))
					? Chain.ModifyBones[Bone.ParentIndex].BoneRef
					: Bone.BoneRef;

			CachedBoneTM = TransformBone.IsValidToEvaluate(BoneContainer)
				               ? Output.Pose.GetComponentSpaceTransform(TransformBone.GetCompactPoseIndex(BoneContainer))
				               : FTransform::Identity;
		}
		return CachedBoneTM.GetValue();
	};

	for (UKawaiiPhysics_CustomExternalForce* Custom : Chain.ExternalForceSettings.CustomExternalForces)
	{
		if (Custom && Custom->bIsEnabled)
		{
			Custom->Apply(*this, Bone.Index, SkelComp, GetBoneTM());
		}
	}

	for (FInstancedStruct& ForceStruct : Chain.ExternalForceSettings.ExternalForces)
	{
		if (FKawaiiPhysics_ExternalForce* Force = ForceStruct.GetMutablePtr<FKawaiiPhysics_ExternalForce>())
		{
			if (Force->bIsEnabled)
			{
				if (Force->ExternalForceSpace == EExternalForceSpace::BoneSpace)
				{
					// Game 0x767A98C: a dummy with no parent skips bone-space forces.
					if (Bone.bDummy && !Chain.ModifyBones.IsValidIndex(Bone.ParentIndex))
					{
						continue;
					}
					Force->Apply(Bone, *this, Chain, Output, GetBoneTM());
				}
				else
				{
					Force->Apply(Bone, *this, Chain, Output);
				}
			}
		}
	}

	// --- wave animation (0x767AAD2): the CHANGE of the wave over this step ---
	// W(T + dt) - W(T) on the node clock, before the stiffness pull.
	if (Chain.WaveAnimSettings.bEnableWaveAnim && Bone.bChildOfWaveBeginBone)
	{
		Bone.Location += GetWaveOffset(Chain, Bone, WaveTime + DeltaTime, bHasFixed) -
			GetWaveOffset(Chain, Bone, WaveTime, bHasFixed);
	}

	// --- pull toward the pose, relative to the simulated parent ---
	// The target is where the bone sits in the pose RELATIVE to its parent,
	// carried along with the parent's simulated position. A simulated root has
	// no parent and falls back to the absolute pose. No SpringStiffness term
	// here: the game applies it in the length restore.
	// 0x767ACC1: in fix-tail mode the pull is skipped; FixTailStiffness does it.
	if (Chain.BoneSettings.bShouldFixTailBone && bHasFixed)
	{
		return;
	}

	FVector BaseLocation = Bone.PoseLocation;
	if (Chain.ModifyBones.IsValidIndex(Bone.ParentIndex))
	{
		const FKawaiiPhysicsModifyBone& ParentBone = Chain.ModifyBones[Bone.ParentIndex];
		BaseLocation = ParentBone.Location + (Bone.PoseLocation - ParentBone.PoseLocation);
	}
	Bone.Location += (BaseLocation - Bone.Location) * AdjustRateByExponent(Settings.Stiffness, Exponent);
}

const FKawaiiPhysicsModifyBone* FAnimNode_KawaiiPhysics::GetSimParent(const FKawaiiPhysicsChain& Chain,
                                                                      const FKawaiiPhysicsModifyBone& Bone)
{
	if (Chain.ModifyBones.IsValidIndex(Bone.ParentIndex))
	{
		return &Chain.ModifyBones[Bone.ParentIndex];
	}
	if (!Bone.HasParent() && Chain.BoneSettings.bRootBoneSimulate && Chain.bHasVirtualParent)
	{
		return &Chain.VirtualParentBone;
	}
	return nullptr;
}

// The game's fix-tail re-solve (SimulateModifyBones, 0x767BF71-0x767CD0E and
// 0x767D446-0x767E2D6). Each stage sweeps forward (root -> tip, toward the
// parent) and backward (tip -> root, toward the first child, the fixed bone
// pulled to the follow bone), then blends the two per bone by
// w = (Total - LengthFromRoot) / Total: the root end follows the forward sweep,
// the tail end the backward one. Both sweeps chain their correction through the
// INDEX-ADJACENT slot (i - 1 / i + 1), not the parent's or child's slot, exactly
// as the game does; for the depth-first linear chains this mode is used on,
// those are the same bone.
void FAnimNode_KawaiiPhysics::FixTailStiffness(FKawaiiPhysicsChain& Chain, float Exponent)
{
	KawaiiPhysics::FixTailStiffnessSweep(Chain.ModifyBones, Chain.TotalBoneLength,
	                                     Chain.FollowTargetTransform.GetLocation(), Exponent);
}

void FAnimNode_KawaiiPhysics::FixTailLengths(FKawaiiPhysicsChain& Chain)
{
	KawaiiPhysics::FixTailLengthSweep(Chain.ModifyBones, Chain.TotalBoneLength,
	                                  Chain.BoneSettings.bRootBoneSimulate && Chain.bHasVirtualParent
		                                  ? &Chain.VirtualParentBone
		                                  : nullptr);
}

FVector FAnimNode_KawaiiPhysics::GetWaveOffset(const FKawaiiPhysicsChain& Chain, const FKawaiiPhysicsModifyBone& Bone,
                                               float Time, bool bHasFixed) const
{
	const FWaveAnimSettings& Wave = Chain.WaveAnimSettings;
	const FVector Direction = Wave.WaveDirection.GetSafeNormal(1e-8f);

	// Position along the waving span. With a fixed tail the game mirrors it
	// about the middle of the span, so both ends are nodes of the wave.
	const float Begin = Chain.WaveBeginBoneLengthFromRoot;
	const float Total = Chain.TotalBoneLength;
	const float Length = Bone.LengthFromRoot;
	float Along = Length - Begin;
	if (Chain.BoneSettings.bShouldFixTailBone && bHasFixed)
	{
		Along = Length > (Begin + Total) * 0.5f ? Total - Length : Length - Begin;
	}
	const float Rate = FMath::Max(Along / (Total + 1e-4f - Begin), 0.0f);

	const float Phase = 2.0f * PI * (Wave.WaveFrequncy * Time + Wave.WaveNum * Rate);
	return Direction * (Bone.WaveAmplitude * FMath::Sin(Phase));
}

FVector FAnimNode_KawaiiPhysics::GetWindVelocity(const FKawaiiPhysicsChain& Chain, const FSceneInterface* Scene,
                                                 const FTransform& ComponentTransform,
                                                 const FKawaiiPhysicsModifyBone& Bone) const
{
	if (!Scene)
	{
		return FVector::ZeroVector;
	}

	// Sampled at the bone's POSE location, as upstream does.
	const FVector WorldLocation = ComponentTransform.TransformPosition(Bone.PoseLocation);

	FVector WindDirection = FVector::ZeroVector;
	float WindSpeed = 0.0f;
	float WindMinGust = 0.0f;
	float WindMaxGust = 0.0f;
	Scene->GetWindParameters_GameThread(WorldLocation, WindDirection, WindSpeed, WindMinGust, WindMaxGust);

	FVector WindVelocity = ComponentTransform.InverseTransformVector(WindDirection) * WindSpeed *
		Chain.ExternalForceSettings.WindScale;

	// Upstream's gust model (copied there from AnimDynamics): a per-bone random
	// scale in [0, 2] each evaluation.
	WindVelocity *= FMath::FRandRange(0.0f, 2.0f);

	return WindVelocity;
}

///////////////////////////////////////////////////////////////////////////////
// Output
///////////////////////////////////////////////////////////////////////////////

void FAnimNode_KawaiiPhysics::ApplySimulateResult(FKawaiiPhysicsChain& Chain, FComponentSpacePoseContext& Output,
                                                  const FBoneContainer& BoneContainer,
                                                  TArray<FBoneTransform>& OutBoneTransforms)
{
	for (const FKawaiiPhysicsModifyBone& Bone : Chain.ModifyBones)
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

		// One entry per bone; a duplicate index would trip the engine's check.
		if (OutBoneTransforms.ContainsByPredicate(
			[CompactPoseIndex](const FBoneTransform& Transform) { return Transform.BoneIndex == CompactPoseIndex; }))
		{
			continue;
		}

		FTransform NewTransform = Output.Pose.GetComponentSpaceTransform(CompactPoseIndex);
		// A bone with no parent keeps its pose location, as upstream: bone
		// constraints may have nudged its simulated position this frame, but
		// upstream never writes that back.
		// Game 0x7672A44: the simulated location (a pinned root's is its pose),
		// unless the bone's direction from its parent did not change.
		NewTransform.SetLocation(Bone.bKeepPoseLocation ? Bone.PoseLocation : Bone.Location);
		NewTransform.SetRotation(Bone.Rotation);

		OutBoneTransforms.Add(FBoneTransform(CompactPoseIndex, NewTransform));
	}
}

///////////////////////////////////////////////////////////////////////////////
// Deprecated data upgrade
///////////////////////////////////////////////////////////////////////////////

void FAnimNode_KawaiiPhysics::UpgradeDeprecatedProperties()
{
	// Only fires for data authored before the Chains refactor. Anything with a
	// populated chain has already been through this.
	const bool bHasLegacyData = RootBone_DEPRECATED.BoneName != NAME_None;
	const bool bHasChainData = Chains.ContainsByPredicate(
		[](const FKawaiiPhysicsChain& Chain) { return Chain.BoneSettings.RootBone.BoneName != NAME_None; });

	if (!bHasLegacyData || bHasChainData)
	{
		return;
	}

	if (Chains.IsEmpty())
	{
		Chains.AddDefaulted();
	}

	FKawaiiPhysicsChain& Chain = Chains[0];

	FBoneSettings& Bones = Chain.BoneSettings;
	Bones.RootBone = RootBone_DEPRECATED;
	Bones.ExcludeBones = ExcludeBones_DEPRECATED;
	Bones.bRootBoneSimulate = bRootBoneSimulate_DEPRECATED;
	Bones.bShouldFixTailBone = bShouldFixTailBone_DEPRECATED;
	Bones.FixedBone = FixedBone_DEPRECATED;
	Bones.FollowBone = FollowBone_DEPRECATED;
	Bones.AdditionalRootBones = AdditionalRootBones_DEPRECATED;
	Bones.DummyBoneLength = DummyBoneLength_DEPRECATED;
	Bones.BoneForwardAxis = BoneForwardAxis_DEPRECATED;

	FBoneChainPhysicsSettings& Physics = Chain.PhysicsSettings;
	Physics.PhysicsSettings = PhysicsSettings_DEPRECATED;
	Physics.CustomShapes = CustomShapes_DEPRECATED;
	Physics.bUseCurve = bUseCurve_DEPRECATED;
	Physics.PhysicsParamsPerBone = PhysicsParamsPerBone_DEPRECATED;
	Physics.TeleportDistanceThreshold = TeleportDistanceThreshold_DEPRECATED;
	Physics.TeleportRotationThreshold = TeleportRotationThreshold_DEPRECATED;
	Physics.PlanarConstraint = PlanarConstraint_DEPRECATED;
	Physics.LimitLinearCurveData = LimitLinearCurveData_DEPRECATED;
	Physics.GravityCurveData = GravityCurveData_DEPRECATED;
	Physics.DampingCurveData = DampingCurveData_DEPRECATED;
	Physics.StiffnessCurveData = StiffnessCurveData_DEPRECATED;
	Physics.WorldDampingLocationCurveData = WorldDampingLocationCurveData_DEPRECATED;
	Physics.WorldDampingRotationCurveData = WorldDampingRotationCurveData_DEPRECATED;
	Physics.RadiusCurveData = RadiusCurveData_DEPRECATED;
	Physics.LimitAngleCurveData = LimitAngleCurveData_DEPRECATED;

	FBoneConstraintSettings& Constraints = Chain.BoneConstraintSettings;
	Constraints.BoneConstraintGlobalComplianceType = BoneConstraintGlobalComplianceType_DEPRECATED;
	Constraints.BoneConstraintIterationCountBeforeCollision = BoneConstraintIterationCountBeforeCollision_DEPRECATED;
	Constraints.BoneConstraintIterationCountAfterCollision = BoneConstraintIterationCountAfterCollision_DEPRECATED;
	Constraints.bAutoAddChildDummyBoneConstraint = bAutoAddChildDummyBoneConstraint_DEPRECATED;
	Constraints.BoneConstraints = BoneConstraints_DEPRECATED;
	Constraints.BoneConstraintsDataAsset = BoneConstraintsDataAsset_DEPRECATED;
	Constraints.BoneConstraintsData = BoneConstraintsData_DEPRECATED;

	FExternalForceSettings& Forces = Chain.ExternalForceSettings;
	Forces.Gravity = Gravity_DEPRECATED;
	Forces.bEnableWind = bEnableWind_DEPRECATED;
	Forces.WindScale = WindScale_DEPRECATED;
	Forces.ExternalForces = ExternalForces_DEPRECATED;
	Forces.CustomExternalForces = CustomExternalForces_DEPRECATED;

	FWaveAnimSettings& Wave = Chain.WaveAnimSettings;
	Wave.bEnableWaveAnim = bEnableWaveAnim_DEPRECATED;
	Wave.WaveBeginBone = WaveBeginBone_DEPRECATED;
	Wave.WaveFrequncy = WaveFrequncy_DEPRECATED;
	Wave.WaveNum = WaveNum_DEPRECATED;
	Wave.WaveDirection = WaveDirection_DEPRECATED;
	Wave.WaveAmplitude = WaveAmplitude_DEPRECATED;
	Wave.WaveAmplitudeCurveData = WaveAmplitudeCurveData_DEPRECATED;

	// The pre-RuntimeFloatCurve UCurveFloat assets are older still. Sampling
	// them here would need the asset loaded on a worker thread, so they are
	// intentionally left for the editor-side upgrade to surface.
	UE_CLOG(DampingCurve_DEPRECATED || StiffnessCurve_DEPRECATED || RadiusCurve_DEPRECATED ||
	        LimitAngleCurve_DEPRECATED || WorldDampingLocationCurve_DEPRECATED || WorldDampingRotationCurve_DEPRECATED,
	        LogKawaiiPhysics, Warning,
	        TEXT("KawaiiPhysics: node still references legacy UCurveFloat assets. "
		        "Re-author these as inline curves; they are not migrated automatically."));
}
