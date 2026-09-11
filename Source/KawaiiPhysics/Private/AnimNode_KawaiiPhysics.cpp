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

		for (FInstancedStruct& ForceStruct : Chain.ExternalForceSettings.ExternalForces)
		{
			if (FKawaiiPhysics_ExternalForce* Force = ForceStruct.GetMutablePtr<FKawaiiPhysics_ExternalForce>())
			{
				Force->Initialize(Context);
			}
		}
	}

	bInitPhysicsSettings = false;
	bResetDynamics = false;
}

void FAnimNode_KawaiiPhysics::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	Super::CacheBones_AnyThread(Context);

	// The required-bone set changed, so every cached index is stale.
	for (FKawaiiPhysicsChain& Chain : Chains)
	{
		Chain.bInitialized = false;
	}
}

void FAnimNode_KawaiiPhysics::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
	for (FKawaiiPhysicsChain& Chain : Chains)
	{
		FBoneSettings& Bones = Chain.BoneSettings;

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

	for (const FKawaiiPhysicsChain& Chain : Chains)
	{
		if (Chain.BoneSettings.RootBone.IsValidToEvaluate(RequiredBones))
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

	bResetDynamics = true;
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
	const FBoneContainer& BoneContainer = Output.Pose.GetPose().GetBoneContainer();
	FTransform ComponentTransform = Output.AnimInstanceProxy->GetComponentTransform();

#if WITH_EDITORONLY_DATA
	LastEvaluatedTime = FPlatformTime::Seconds();
#endif

	// Limits are shared across chains, so resolve them once per frame.
	UpdateSphericalLimits(SphericalLimits, Output, BoneContainer, ComponentTransform);
	UpdateCapsuleLimits(CapsuleLimits, Output, BoneContainer, ComponentTransform);
	UpdateBoxLimits(BoxLimits, Output, BoneContainer, ComponentTransform);
	UpdatePlanerLimits(PlanarLimits, Output, BoneContainer, ComponentTransform);
	UpdateSphericalLimits(SphericalLimitsData, Output, BoneContainer, ComponentTransform);
	UpdateCapsuleLimits(CapsuleLimitsData, Output, BoneContainer, ComponentTransform);
	UpdateBoxLimits(BoxLimitsData, Output, BoneContainer, ComponentTransform);
	UpdatePlanerLimits(PlanarLimitsData, Output, BoneContainer, ComponentTransform);

	const bool bWantWarmUp = (bNeedWarmUp && WarmUpFrames > 0) &&
		(!bInitPhysicsSettings || (bResetDynamics && bUseWarmUpWhenResetDynamics));

	for (FKawaiiPhysicsChain& Chain : Chains)
	{
		if (!Chain.IsEnabledForLOD(CachedLODLevel))
		{
			continue;
		}

		if (!Chain.BoneSettings.RootBone.IsValidToEvaluate(BoneContainer))
		{
			continue;
		}

		if (!Chain.bInitialized || Chain.ModifyBones.IsEmpty())
		{
			InitModifyBones(Chain, Output, BoneContainer);
			if (Chain.ModifyBones.IsEmpty())
			{
				continue;
			}
		}

		UpdateModifyBonesPoseTransform(Chain, Output, BoneContainer);

		if (bResetDynamics)
		{
			// Snap the simulation onto the incoming pose rather than letting it
			// spring across the teleport.
			for (FKawaiiPhysicsModifyBone& Bone : Chain.ModifyBones)
			{
				Bone.Location = Bone.PoseLocation;
				Bone.PrevLocation = Bone.PoseLocation;
				Bone.Rotation = Bone.PoseRotation;
				Bone.PrevRotation = Bone.PoseRotation;
			}
		}

		if (bUpdatePhysicsSettingsInGame || !bInitPhysicsSettings)
		{
			UpdatePhysicsSettingsOfModifyBones(Chain);
		}

		UpdateSkelCompMove(Chain, ComponentTransform);

		if (bWantWarmUp)
		{
			// Handled once for every chain below, after this loop primes them.
			continue;
		}

		SimulateModifyBones(Chain, Output, ComponentTransform);
		ApplySimulateResult(Chain, Output, BoneContainer, OutBoneTransforms);
	}

	if (bWantWarmUp)
	{
		WarmUp(Output, BoneContainer, ComponentTransform);

		for (FKawaiiPhysicsChain& Chain : Chains)
		{
			if (Chain.IsEnabledForLOD(CachedLODLevel) && !Chain.ModifyBones.IsEmpty())
			{
				ApplySimulateResult(Chain, Output, BoneContainer, OutBoneTransforms);
			}
		}
	}

	bInitPhysicsSettings = true;
	bResetDynamics = false;
	DeltaTimeOld = DeltaTime;
	PreSkelCompTransform = ComponentTransform;

	// FAnimNode_SkeletalControlBase requires strictly increasing bone indices.
	OutBoneTransforms.Sort(FCompareBoneTransformIndex());

#if ENABLE_ANIM_DEBUG
	AnimDrawDebug(Output);
#endif
}

void FAnimNode_KawaiiPhysics::WarmUp(FComponentSpacePoseContext& Output, const FBoneContainer& BoneContainer,
                                     FTransform& ComponentTransform)
{
	// Burn frames at the target rate so the chains settle before anything is shown.
	const float WarmUpDeltaTime = 1.0f / FMath::Max(1, TargetFramerate);

	const float SavedDeltaTime = DeltaTime;
	const float SavedDeltaTimeOld = DeltaTimeOld;
	const FVector SavedMoveVector = SkelCompMoveVector;
	const FQuat SavedMoveRotation = SkelCompMoveRotation;

	DeltaTime = WarmUpDeltaTime;
	DeltaTimeOld = WarmUpDeltaTime;
	// The component is not actually moving during warm-up.
	SkelCompMoveVector = FVector::ZeroVector;
	SkelCompMoveRotation = FQuat::Identity;

	for (int32 Frame = 0; Frame < WarmUpFrames; ++Frame)
	{
		for (FKawaiiPhysicsChain& Chain : Chains)
		{
			if (Chain.IsEnabledForLOD(CachedLODLevel) && !Chain.ModifyBones.IsEmpty())
			{
				SimulateModifyBones(Chain, Output, ComponentTransform);
			}
		}
	}

	DeltaTime = SavedDeltaTime;
	DeltaTimeOld = SavedDeltaTimeOld;
	SkelCompMoveVector = SavedMoveVector;
	SkelCompMoveRotation = SavedMoveRotation;
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
		Bone.LengthFromRoot = 0.0f;
	}
	else
	{
		const FKawaiiPhysicsModifyBone& Parent = Chain.ModifyBones[Bone.ParentIndex];
		Bone.LengthFromRoot = Parent.LengthFromRoot + (Bone.PoseLocation - Parent.PoseLocation).Size();
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
	Chain.FollowBoneIndex = INDEX_NONE;

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

	MarkFixedAndFollowBones(Chain);
	MarkWaveBones(Chain);
	InitBoneConstraints(Chain);

	Chain.bInitialized = true;
	bInitPhysicsSettings = false;
}

void FAnimNode_KawaiiPhysics::MarkFixedAndFollowBones(FKawaiiPhysicsChain& Chain)
{
	const FBoneSettings& Bones = Chain.BoneSettings;

	for (FKawaiiPhysicsModifyBone& Bone : Chain.ModifyBones)
	{
		Bone.bFixed = false;
		Bone.bHasFollowBone = false;

		// A root is pinned unless it was explicitly opted into simulation.
		if (!Bone.HasParent() && !Bones.bRootBoneSimulate)
		{
			Bone.bFixed = true;
		}

		// The last real bone, when asked for.
		if (Bones.bShouldFixTailBone && !Bone.bDummy)
		{
			const bool bHasRealChild = Bone.ChildIndices.ContainsByPredicate(
				[&Chain](int32 Index) { return !Chain.ModifyBones[Index].bDummy; });
			if (!bHasRealChild)
			{
				Bone.bFixed = true;
			}
		}

		if (Bones.FixedBone.BoneName != NAME_None && Bone.BoneRef.BoneName == Bones.FixedBone.BoneName)
		{
			Bone.bFixed = true;
		}

		if (Bones.FollowBone.BoneName != NAME_None && Bone.BoneRef.BoneName == Bones.FollowBone.BoneName)
		{
			Bone.bHasFollowBone = true;
			Chain.FollowBoneIndex = Bone.Index;
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

		if (!bAffected)
		{
			Bone.WaveAmplitude = 0.0f;
			continue;
		}

		// Position along the waving span, normalised so the curve reads 0..1.
		const float Span = FMath::Max(KINDA_SMALL_NUMBER, Chain.TotalBoneLength - Chain.WaveBeginBoneLengthFromRoot);
		const float SpanRate = FMath::Clamp((Bone.LengthFromRoot - Chain.WaveBeginBoneLengthFromRoot) / Span, 0.0f,
		                                    1.0f);

		float AmplitudeRate = 1.0f;
		if (const FRichCurve* Curve = Wave.WaveAmplitudeCurveData.GetRichCurveConst())
		{
			if (Curve->GetNumKeys() > 0)
			{
				AmplitudeRate = Curve->Eval(SpanRate);
			}
		}

		Bone.WaveAmplitude = Wave.WaveAmplitude * AmplitudeRate;
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

	for (int32 Index = 0; Index < Chain.ModifyBones.Num(); ++Index)
	{
		FKawaiiPhysicsModifyBone& Bone = Chain.ModifyBones[Index];
		const float Rate = Bone.LengthRateFromRoot;

		FKawaiiPhysicsSettings& Out = Bone.PhysicsSettings;
		Out = Shared;

		if (Settings.bUseCurve || Settings.PhysicsParamsPerBone.IsEmpty())
		{
			Out.Damping = EvalCurve(Settings.DampingCurveData, Rate, Shared.Damping);
			Out.Stiffness = EvalCurve(Settings.StiffnessCurveData, Rate, Shared.Stiffness);
			Out.WorldDampingLocation = EvalCurve(Settings.WorldDampingLocationCurveData, Rate,
			                                     Shared.WorldDampingLocation);
			Out.WorldDampingRotation = EvalCurve(Settings.WorldDampingRotationCurveData, Rate,
			                                     Shared.WorldDampingRotation);
			Out.Radius = EvalCurve(Settings.RadiusCurveData, Rate, Shared.Radius);
			Out.LimitAngle = EvalCurve(Settings.LimitAngleCurveData, Rate, Shared.LimitAngle);
			Out.LimitLinear = EvalCurve(Settings.LimitLinearCurveData, Rate, Shared.LimitLinear);
			Out.GravityScale = EvalCurve(Settings.GravityCurveData, Rate, Shared.GravityScale);
		}
		else if (Settings.PhysicsParamsPerBone.IsValidIndex(Index))
		{
			// Per-bone overrides are positional against ModifyBones; each field
			// falls back to the shared value unless its toggle is set.
			const FBonePhysicsParams& Params = Settings.PhysicsParamsPerBone[Index];

			if (Params.bNeedEditDamping) Out.Damping = Params.Damping;
			if (Params.bNeedEditStiffness) Out.Stiffness = Params.Stiffness;
			if (Params.bNeedEditWorldDampingLocation) Out.WorldDampingLocation = Params.WorldDampingLocation;
			if (Params.bNeedEditWorldDampingRotation) Out.WorldDampingRotation = Params.WorldDampingRotation;
			if (Params.bNeedEditRadius) Out.Radius = Params.Radius;
			if (Params.bNeedEditLimitAngle) Out.LimitAngle = Params.LimitAngle;
			if (Params.bNeedEditLimitLinear) Out.LimitLinear = Params.LimitLinear;
			if (Params.bNeedEditGravityScale) Out.GravityScale = Params.GravityScale;
		}

		Out.Damping = FMath::Clamp(Out.Damping, 0.0f, 1.0f);
		Out.Stiffness = FMath::Clamp(Out.Stiffness, 0.0f, 1.0f);
		Out.WorldDampingLocation = FMath::Clamp(Out.WorldDampingLocation, 0.0f, 1.0f);
		Out.WorldDampingRotation = FMath::Clamp(Out.WorldDampingRotation, 0.0f, 1.0f);
		Out.Radius = FMath::Max(0.0f, Out.Radius);
	}
}

void FAnimNode_KawaiiPhysics::UpdateModifyBonesPoseTransform(FKawaiiPhysicsChain& Chain,
                                                             FComponentSpacePoseContext& Output,
                                                             const FBoneContainer& BoneContainer)
{
	for (FKawaiiPhysicsModifyBone& Bone : Chain.ModifyBones)
	{
		if (!Bone.bDummy)
		{
			Bone.UpdatePoseTransform(BoneContainer, Output.Pose, ResetBoneTransformWhenBoneNotFound);
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
	SkelCompMoveVector = ComponentTransform.InverseTransformPosition(PreSkelCompTransform.GetLocation());
	SkelCompMoveRotation = ComponentTransform.InverseTransformRotation(PreSkelCompTransform.GetRotation());

	// A teleport is not motion the cloth should react to; drop it entirely.
	const float DistanceThreshold = Chain.PhysicsSettings.TeleportDistanceThreshold;
	if (DistanceThreshold > 0.0f && SkelCompMoveVector.SizeSquared() > FMath::Square(DistanceThreshold))
	{
		SkelCompMoveVector = FVector::ZeroVector;
	}

	const float RotationThreshold = Chain.PhysicsSettings.TeleportRotationThreshold;
	if (RotationThreshold > 0.0f &&
		FMath::RadiansToDegrees(SkelCompMoveRotation.GetAngle()) > RotationThreshold)
	{
		SkelCompMoveRotation = FQuat::Identity;
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

	Chain.WaveTime += DeltaTime;

	for (FInstancedStruct& ForceStruct : Chain.ExternalForceSettings.ExternalForces)
	{
		if (FKawaiiPhysics_ExternalForce* Force = ForceStruct.GetMutablePtr<FKawaiiPhysics_ExternalForce>())
		{
			if (Force->bIsEnabled)
			{
				Force->PreApply(*this, Chain, SkelComp);
			}
		}
	}

	for (UKawaiiPhysics_CustomExternalForce* Custom : Chain.ExternalForceSettings.CustomExternalForces)
	{
		if (Custom && Custom->bIsEnabled)
		{
			Custom->PreApply(*this, SkelComp);
		}
	}

	for (FKawaiiPhysicsModifyBone& Bone : Chain.ModifyBones)
	{
		Simulate(Chain, Bone, Scene, ComponentTransform, GravityCS, Exponent, SkelComp, Output);
	}

	// Constraints before collision, so collision gets the last word on penetration.
	AdjustByBoneConstraints(Chain, Chain.BoneConstraintSettings.BoneConstraintIterationCountBeforeCollision);

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

		if (Bone.HasParent())
		{
			const FKawaiiPhysicsModifyBone& Parent = Chain.ModifyBones[Bone.ParentIndex];
			AdjustByAngleLimit(Chain, Bone, Parent);
			AdjustByPlanarConstraint(Chain, Bone, Parent);
		}

		AdjustByLinearLimit(Bone);
	}

	AdjustByBoneConstraints(Chain, Chain.BoneConstraintSettings.BoneConstraintIterationCountAfterCollision);

	// Orientation is derived last, once every position is final.
	for (FKawaiiPhysicsModifyBone& Bone : Chain.ModifyBones)
	{
		if (Bone.bDummy)
		{
			continue;
		}

		FQuat NewRotation = Bone.PoseRotation;

		// Point the bone's forward axis at the average of its children.
		if (Bone.ChildIndices.Num() > 0)
		{
			FVector ChildCentre = FVector::ZeroVector;
			for (const int32 ChildIndex : Bone.ChildIndices)
			{
				ChildCentre += Chain.ModifyBones[ChildIndex].Location;
			}
			ChildCentre /= static_cast<float>(Bone.ChildIndices.Num());

			const FVector PoseDirection =
				GetBoneForwardVector(Bone.PoseRotation, Chain.BoneSettings.BoneForwardAxis);
			const FVector SimDirection = (ChildCentre - Bone.Location).GetSafeNormal();

			if (!SimDirection.IsNearlyZero() && !PoseDirection.IsNearlyZero())
			{
				NewRotation = FQuat::FindBetweenNormals(PoseDirection, SimDirection) * Bone.PoseRotation;
			}
		}

		Bone.PrevRotation = Bone.Rotation;
		Bone.Rotation = Bone.OffsetRotation.IsIdentity() ? NewRotation : NewRotation * Bone.OffsetRotation;
	}

	for (FKawaiiPhysicsModifyBone& Bone : Chain.ModifyBones)
	{
		if (Bone.HasParent())
		{
			AdjustByRelativeRotationLimit(Chain, Bone, Chain.ModifyBones[Bone.ParentIndex]);
		}
	}

	for (FInstancedStruct& ForceStruct : Chain.ExternalForceSettings.ExternalForces)
	{
		if (FKawaiiPhysics_ExternalForce* Force = ForceStruct.GetMutablePtr<FKawaiiPhysics_ExternalForce>())
		{
			Force->PostApply(*this, Chain);
		}
	}
}

void FAnimNode_KawaiiPhysics::Simulate(FKawaiiPhysicsChain& Chain, FKawaiiPhysicsModifyBone& Bone,
                                       const FSceneInterface* Scene, const FTransform& ComponentTransform,
                                       const FVector& GravityCS, const float& Exponent,
                                       const USkeletalMeshComponent* SkelComp, FComponentSpacePoseContext& Output)
{
	if (!Bone.IsSimulated())
	{
		// Pinned bones ride the pose exactly, but still track history so that a
		// later unpin does not produce a huge synthetic velocity.
		Bone.Location = Bone.PoseLocation;
		Bone.PrevLocation = Bone.PoseLocation;
		return;
	}

	const FKawaiiPhysicsSettings& Settings = Bone.PhysicsSettings;

	// --- integrate ---
	FVector Velocity = FVector::ZeroVector;
	if (DeltaTimeOld > 0.0f)
	{
		Velocity = (Bone.Location - Bone.PrevLocation) / DeltaTimeOld;
	}

	Bone.PrevLocation = Bone.Location;

	Velocity *= 1.0f - AdjustRateByExponent(Settings.Damping, Exponent);

	Bone.Location += Velocity * DeltaTime;
	Bone.Location += GravityCS * Settings.GravityScale * DeltaTime * DeltaTime;

	// --- carry the component's own motion ---
	Bone.Location += SkelCompMoveVector * (1.0f - AdjustRateByExponent(Settings.WorldDampingLocation, Exponent));
	Bone.Location += (SkelCompMoveRotation.RotateVector(Bone.Location) - Bone.Location) *
		(1.0f - AdjustRateByExponent(Settings.WorldDampingRotation, Exponent));

	// --- pull back toward the animated pose ---
	Bone.Location += (Bone.PoseLocation - Bone.Location) * AdjustRateByExponent(Settings.Stiffness, Exponent);

	// A spring is authored per bone rather than per chain, so it is read from
	// the override array directly.
	const FBoneChainPhysicsSettings& ChainPhysics = Chain.PhysicsSettings;
	if (!ChainPhysics.bUseCurve && ChainPhysics.PhysicsParamsPerBone.IsValidIndex(Bone.Index))
	{
		const float SpringStiffness = ChainPhysics.PhysicsParamsPerBone[Bone.Index].SpringStiffness;
		if (SpringStiffness > 0.0f)
		{
			Bone.Location += (Bone.PoseLocation - Bone.Location) * AdjustRateByExponent(SpringStiffness, Exponent);
		}
	}

	// --- external forces ---
	for (FInstancedStruct& ForceStruct : Chain.ExternalForceSettings.ExternalForces)
	{
		if (FKawaiiPhysics_ExternalForce* Force = ForceStruct.GetMutablePtr<FKawaiiPhysics_ExternalForce>())
		{
			if (Force->bIsEnabled)
			{
				Force->Apply(Bone, *this, Chain, Output);
			}
		}
	}

	for (UKawaiiPhysics_CustomExternalForce* Custom : Chain.ExternalForceSettings.CustomExternalForces)
	{
		if (Custom && Custom->bIsEnabled)
		{
			Custom->Apply(*this, Bone.Index, SkelComp, ComponentTransform);
		}
	}

	if (Chain.ExternalForceSettings.bEnableWind && Scene)
	{
		Bone.Location += GetWindVelocity(Chain, Scene, ComponentTransform, Bone) * DeltaTime * DeltaTime;
	}

	ApplyWaveAnim(Chain, Bone);
}

void FAnimNode_KawaiiPhysics::ApplyWaveAnim(FKawaiiPhysicsChain& Chain, FKawaiiPhysicsModifyBone& Bone)
{
	const FWaveAnimSettings& Wave = Chain.WaveAnimSettings;

	if (!Wave.bEnableWaveAnim || !Bone.bChildOfWaveBeginBone || Bone.WaveAmplitude <= 0.0f)
	{
		return;
	}

	const FVector Direction = Wave.WaveDirection.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return;
	}

	// A travelling wave: phase advances with time and recedes along the chain,
	// so the displacement reads as motion running outward from the begin bone.
	const float Span = FMath::Max(KINDA_SMALL_NUMBER, Chain.TotalBoneLength - Chain.WaveBeginBoneLengthFromRoot);
	const float SpanRate = FMath::Clamp((Bone.LengthFromRoot - Chain.WaveBeginBoneLengthFromRoot) / Span, 0.0f, 1.0f);

	const float Phase = 2.0f * PI * (Chain.WaveTime * Wave.WaveFrequncy - SpanRate * Wave.WaveNum);

	Bone.Location += Direction * (Bone.WaveAmplitude * FMath::Sin(Phase));
}

FVector FAnimNode_KawaiiPhysics::GetWindVelocity(const FKawaiiPhysicsChain& Chain, const FSceneInterface* Scene,
                                                 const FTransform& ComponentTransform,
                                                 const FKawaiiPhysicsModifyBone& Bone) const
{
	if (!Scene)
	{
		return FVector::ZeroVector;
	}

	const FVector WorldLocation = ComponentTransform.TransformPosition(Bone.Location);

	FVector WindDirection = FVector::ZeroVector;
	float WindSpeed = 0.0f;
	float WindMinGust = 0.0f;
	float WindMaxGust = 0.0f;
	Scene->GetWindParameters_GameThread(WorldLocation, WindDirection, WindSpeed, WindMinGust, WindMaxGust);

	const FVector WindVelocityWS = WindDirection * WindSpeed * Chain.ExternalForceSettings.WindScale;
	return ComponentTransform.InverseTransformVector(WindVelocityWS);
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
		NewTransform.SetLocation(Bone.Location);
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
