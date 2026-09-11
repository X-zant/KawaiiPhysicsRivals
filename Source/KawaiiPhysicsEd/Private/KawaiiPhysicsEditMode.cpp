// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License

#include "KawaiiPhysicsEditMode.h"

#include "AnimGraphNode_KawaiiPhysics.h"
// GetPreviewMeshComponent() returns UDebugSkelMeshComponent*, which the Persona
// header only forward-declares.
#include "Animation/DebugSkelMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "SceneManagement.h"

namespace
{
	constexpr uint8 DepthPriority = SDPG_Foreground;

	FColor GetLimitColour(ECollisionSourceType SourceType)
	{
		// Colour by provenance, so it is obvious at a glance which limits are
		// authored on the node and which arrived from an asset.
		switch (SourceType)
		{
		case ECollisionSourceType::DataAsset: return FColor(120, 200, 255);
		case ECollisionSourceType::PhysicsAsset: return FColor(255, 190, 120);
		case ECollisionSourceType::AnimNode:
		default: return FColor(160, 255, 160);
		}
	}
}

void FKawaiiPhysicsEditMode::EnterMode(UAnimGraphNode_Base* InEditorNode, FAnimNode_Base* InRuntimeNode)
{
	GraphNode = CastChecked<UAnimGraphNode_KawaiiPhysics>(InEditorNode);
	RuntimeNode = static_cast<FAnimNode_KawaiiPhysics*>(InRuntimeNode);

	FAnimNodeEditMode::EnterMode(InEditorNode, InRuntimeNode);
}

void FKawaiiPhysicsEditMode::ExitMode()
{
	GraphNode = nullptr;
	RuntimeNode = nullptr;

	FAnimNodeEditMode::ExitMode();
}

void FKawaiiPhysicsEditMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	FAnimNodeEditMode::Render(View, Viewport, PDI);

	if (!RuntimeNode || !GraphNode)
	{
		return;
	}

	const USkeletalMeshComponent* MeshComp = GetAnimPreviewScene().GetPreviewMeshComponent();
	if (!MeshComp)
	{
		return;
	}

	const FTransform ComponentTransform = MeshComp->GetComponentTransform();

	RenderChains(PDI, ComponentTransform);
	RenderLimits(PDI, ComponentTransform);
}

void FKawaiiPhysicsEditMode::RenderChains(FPrimitiveDrawInterface* PDI, const FTransform& ComponentTransform) const
{
	for (const FKawaiiPhysicsChain& Chain : RuntimeNode->Chains)
	{
		for (const FKawaiiPhysicsModifyBone& Bone : Chain.ModifyBones)
		{
			const FVector LocationWS = ComponentTransform.TransformPosition(Bone.Location);

			// Pinned bones read differently from simulated ones; a chain that is
			// not moving is usually a chain that is unexpectedly all-pinned.
			const FColor BoneColour = Bone.bDummy
				                          ? FColor(255, 220, 100)
				                          : (Bone.IsSimulated() ? FColor::White : FColor(255, 100, 100));

			DrawWireSphere(PDI, LocationWS, BoneColour, Bone.PhysicsSettings.Radius, 12, DepthPriority);

			if (Bone.HasParent() && Chain.ModifyBones.IsValidIndex(Bone.ParentIndex))
			{
				const FVector ParentWS =
					ComponentTransform.TransformPosition(Chain.ModifyBones[Bone.ParentIndex].Location);
				PDI->DrawLine(ParentWS, LocationWS, FColor(120, 255, 120), DepthPriority);
			}
		}
	}
}

void FKawaiiPhysicsEditMode::RenderLimits(FPrimitiveDrawInterface* PDI, const FTransform& ComponentTransform) const
{
	auto DrawSpheres = [&](const TArray<FSphericalLimit>& Limits)
	{
		for (const FSphericalLimit& Limit : Limits)
		{
			if (!Limit.bEnable) continue;

			const FVector LocationWS = ComponentTransform.TransformPosition(Limit.Location);
			DrawWireSphere(PDI, LocationWS, GetLimitColour(Limit.SourceType), Limit.Radius, 24, DepthPriority);
		}
	};

	auto DrawCapsules = [&](const TArray<FCapsuleLimit>& Limits)
	{
		for (const FCapsuleLimit& Limit : Limits)
		{
			if (!Limit.bEnable) continue;

			const FTransform LimitTM = FTransform(Limit.Rotation, Limit.Location) * ComponentTransform;
			const FColor Colour = GetLimitColour(Limit.SourceType);

			// DrawWireCapsule takes the half-height including the caps, whereas
			// Length is the cylindrical section only.
			DrawWireCapsule(PDI, LimitTM.GetLocation(),
			                LimitTM.GetRotation().GetAxisX(), LimitTM.GetRotation().GetAxisY(),
			                LimitTM.GetRotation().GetAxisZ(),
			                Colour, Limit.Radius, Limit.Length * 0.5f + Limit.Radius, 24, DepthPriority);
		}
	};

	auto DrawBoxes = [&](const TArray<FBoxLimit>& Limits)
	{
		for (const FBoxLimit& Limit : Limits)
		{
			if (!Limit.bEnable) continue;

			const FTransform LimitTM = FTransform(Limit.Rotation, Limit.Location) * ComponentTransform;
			DrawWireBox(PDI, LimitTM.ToMatrixNoScale(), FBox(-Limit.Extent, Limit.Extent),
			            GetLimitColour(Limit.SourceType), DepthPriority);
		}
	};

	auto DrawPlanes = [&](const TArray<FPlanarLimit>& Limits)
	{
		for (const FPlanarLimit& Limit : Limits)
		{
			if (!Limit.bEnable) continue;

			const FTransform LimitTM = FTransform(Limit.Rotation, Limit.Location) * ComponentTransform;
			const FColor Colour = GetLimitColour(Limit.SourceType);

			// A plane is unbounded; draw a patch plus its normal so the facing
			// direction is unambiguous.
			DrawDirectionalArrow(PDI, LimitTM.ToMatrixNoScale(), Colour, 20.0f, 1.0f, DepthPriority);

			const FVector Origin = LimitTM.GetLocation();
			const FVector AxisX = LimitTM.GetRotation().GetAxisX() * 50.0f;
			const FVector AxisY = LimitTM.GetRotation().GetAxisY() * 50.0f;

			PDI->DrawLine(Origin - AxisX - AxisY, Origin + AxisX - AxisY, Colour, DepthPriority);
			PDI->DrawLine(Origin + AxisX - AxisY, Origin + AxisX + AxisY, Colour, DepthPriority);
			PDI->DrawLine(Origin + AxisX + AxisY, Origin - AxisX + AxisY, Colour, DepthPriority);
			PDI->DrawLine(Origin - AxisX + AxisY, Origin - AxisX - AxisY, Colour, DepthPriority);
		}
	};

	DrawSpheres(RuntimeNode->SphericalLimits);
	DrawSpheres(RuntimeNode->SphericalLimitsData);
	DrawCapsules(RuntimeNode->CapsuleLimits);
	DrawCapsules(RuntimeNode->CapsuleLimitsData);
	DrawBoxes(RuntimeNode->BoxLimits);
	DrawBoxes(RuntimeNode->BoxLimitsData);
	DrawPlanes(RuntimeNode->PlanarLimits);
	DrawPlanes(RuntimeNode->PlanarLimitsData);
}
