// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License

#include "KawaiiPhysicsEditMode.h"

#include "AnimGraphNode_KawaiiPhysics.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "EditorModeManager.h"
#include "EditorViewportClient.h"
#include "IPersonaPreviewScene.h"
#include "KawaiiPhysicsEd.h"
#include "KawaiiPhysicsExternalForce.h"
#include "KawaiiPhysicsLimitsDataAsset.h"
#include "SceneManagement.h"
// GetPreviewMeshComponent() returns UDebugSkelMeshComponent*, which the Persona
// header only forward-declares.
#include "Animation/DebugSkelMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

#define LOCTEXT_NAMESPACE "KawaiiPhysicsEditMode"

/** Lets a click in the viewport identify which limit it landed on. */
struct HKawaiiPhysicsHitProxy : HHitProxy
{
	DECLARE_HIT_PROXY()

	HKawaiiPhysicsHitProxy(ECollisionLimitType InType, int32 InIndex, ECollisionSourceType InSourceType)
		: HHitProxy(HPP_Wireframe)
		  , CollisionType(InType)
		  , CollisionIndex(InIndex)
		  , SourceType(InSourceType)
	{
	}

	virtual EMouseCursor::Type GetMouseCursor() override
	{
		return EMouseCursor::Crosshairs;
	}

	ECollisionLimitType CollisionType;
	int32 CollisionIndex;
	ECollisionSourceType SourceType;
};

IMPLEMENT_HIT_PROXY(HKawaiiPhysicsHitProxy, HHitProxy);

namespace
{
	/** Component transform of the preview mesh; limits are stored in its space. */
	FTransform GetPreviewComponentTransform(const IPersonaPreviewScene& Scene)
	{
		const USkeletalMeshComponent* Mesh = Scene.GetPreviewMeshComponent();
		return Mesh ? Mesh->GetComponentTransform() : FTransform::Identity;
	}

	/**
	 * Rotation of the frame a limit's offsets are expressed in -- the driving
	 * bone (or parent component), recovered from the limit's own resolved
	 * values: Rotation = Frame * OffsetRotation, so Frame = Rotation * Offset^-1.
	 * Works for every way a limit can be anchored without re-deriving any of
	 * them here.
	 */
	FQuat GetLimitFrameRotation(const FCollisionLimitBase& Limit)
	{
		return Limit.Rotation * Limit.OffsetRotation.Quaternion().Inverse();
	}
}

FKawaiiPhysicsEditMode::FKawaiiPhysicsEditMode() = default;

void FKawaiiPhysicsEditMode::EnterMode(UAnimGraphNode_Base* InEditorNode, FAnimNode_Base* InRuntimeNode)
{
	RuntimeNode = static_cast<FAnimNode_KawaiiPhysics*>(InRuntimeNode);
	GraphNode = CastChecked<UAnimGraphNode_KawaiiPhysics>(InEditorNode);

	// Show the asset-derived limits in the details panel too.
	GraphNode->Node.SphericalLimitsData = RuntimeNode->SphericalLimitsData;
	GraphNode->Node.CapsuleLimitsData = RuntimeNode->CapsuleLimitsData;
	GraphNode->Node.BoxLimitsData = RuntimeNode->BoxLimitsData;
	GraphNode->Node.PlanarLimitsData = RuntimeNode->PlanarLimitsData;

	NodePropertyDelegateHandle = GraphNode->OnNodePropertyChanged().AddSP(
		this, &FKawaiiPhysicsEditMode::OnExternalNodePropertyChange);
	BindLimitsDataAsset();

	// Limits generated from a physics asset are shown in the PhAT body colour
	// and are read-only here: editing them means editing the physics asset.
	if (UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Engine/EditorMaterials/PhAT_UnselectedMaterial.PhAT_UnselectedMaterial")))
	{
		PhysicsAssetBodyMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, GetTransientPackage());
		PhysicsAssetBodyMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.2f);
	}

	FAnimNodeEditMode::EnterMode(InEditorNode, InRuntimeNode);
}

void FKawaiiPhysicsEditMode::ExitMode()
{
	if (GraphNode)
	{
		GraphNode->OnNodePropertyChanged().Remove(NodePropertyDelegateHandle);
	}
	UnbindLimitsDataAsset();

	GraphNode = nullptr;
	RuntimeNode = nullptr;
	PhysicsAssetBodyMaterial = nullptr;

	FAnimNodeEditMode::ExitMode();
}

void FKawaiiPhysicsEditMode::AddReferencedObjects(FReferenceCollector& Collector)
{
	FAnimNodeEditMode::AddReferencedObjects(Collector);
	Collector.AddReferencedObject(PhysicsAssetBodyMaterial);
}

void FKawaiiPhysicsEditMode::BindLimitsDataAsset()
{
	UnbindLimitsDataAsset();
	if (RuntimeNode && RuntimeNode->LimitsDataAsset)
	{
		BoundLimitsDataAsset = RuntimeNode->LimitsDataAsset;
		LimitsDataAssetPropertyDelegateHandle = BoundLimitsDataAsset->OnLimitsChanged.AddRaw(
			this, &FKawaiiPhysicsEditMode::OnLimitDataAssetPropertyChange);
	}
}

void FKawaiiPhysicsEditMode::UnbindLimitsDataAsset()
{
	if (UKawaiiPhysicsLimitsDataAsset* Asset = BoundLimitsDataAsset.Get())
	{
		Asset->OnLimitsChanged.Remove(LimitsDataAssetPropertyDelegateHandle);
	}
	BoundLimitsDataAsset.Reset();
	LimitsDataAssetPropertyDelegateHandle.Reset();
}

///////////////////////////////////////////////////////////////////////////////
// Rendering
///////////////////////////////////////////////////////////////////////////////

void FKawaiiPhysicsEditMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	const USkeletalMeshComponent* SkelMeshComp = GetAnimPreviewScene().GetPreviewMeshComponent();

	if (RuntimeNode && GraphNode && SkelMeshComp && SkelMeshComp->GetSkeletalMeshAsset() &&
		FAnimWeight::IsRelevant(RuntimeNode->GetAlpha()) && RuntimeNode->IsRecentlyEvaluated())
	{
		RenderModifyBones(PDI);
		RenderLimitAngle(PDI);
		RenderSphericalLimits(PDI);
		RenderCapsuleLimits(PDI);
		RenderBoxLimits(PDI);
		RenderPlanarLimits(PDI);
		RenderBoneConstraints(PDI);
		RenderExternalForces(PDI);

		PDI->SetHitProxy(nullptr);

		// Tie the selected limit back to what drives it.
		if (const FCollisionLimitBase* Collision = GetSelectCollisionLimitRuntime())
		{
			const FTransform ComponentTransform = GetPreviewComponentTransform(GetAnimPreviewScene());
			const FQuat FrameRotation = GetLimitFrameRotation(*Collision);
			const FVector FrameOrigin = Collision->Location - FrameRotation.RotateVector(Collision->OffsetLocation);

			const FVector FrameOriginWS = ComponentTransform.TransformPosition(FrameOrigin);
			const FVector LimitWS = ComponentTransform.TransformPosition(Collision->Location);

			PDI->DrawPoint(FrameOriginWS, FLinearColor::White, 10.0f, SDPG_Foreground);
			DrawDashedLine(PDI, LimitWS, FrameOriginWS, FLinearColor::White, 1, SDPG_Foreground);
			DrawCoordinateSystem(PDI, FrameOriginWS, (ComponentTransform.GetRotation() * FrameRotation).Rotator(),
			                     20, SDPG_World + 1);
		}
	}

	FAnimNodeEditMode::Render(View, Viewport, PDI);
}

const FMaterialRenderProxy* FKawaiiPhysicsEditMode::GetLimitMaterial(ECollisionSourceType SourceType) const
{
	switch (SourceType)
	{
	case ECollisionSourceType::DataAsset:
		return GEngine->ConstraintLimitMaterialZ ? GEngine->ConstraintLimitMaterialZ->GetRenderProxy() : nullptr;
	case ECollisionSourceType::PhysicsAsset:
		return PhysicsAssetBodyMaterial ? PhysicsAssetBodyMaterial->GetRenderProxy() : nullptr;
	case ECollisionSourceType::AnimNode:
	default:
		// The translucent orange used for constraint limits throughout the editor.
		return GEngine->ConstraintLimitMaterialPrismatic
			       ? GEngine->ConstraintLimitMaterialPrismatic->GetRenderProxy()
			       : nullptr;
	}
}

void FKawaiiPhysicsEditMode::RenderModifyBones(FPrimitiveDrawInterface* PDI) const
{
	if (!GraphNode->bEnableDebugDrawBone)
	{
		return;
	}

	const FTransform ComponentTransform = GetPreviewComponentTransform(GetAnimPreviewScene());
	for (const FKawaiiPhysicsChain& Chain : RuntimeNode->Chains)
	{
		for (const FKawaiiPhysicsModifyBone& Bone : Chain.ModifyBones)
		{
			const FVector LocationWS = ComponentTransform.TransformPosition(Bone.Location);
			PDI->DrawPoint(LocationWS, FLinearColor::White, 5.0f, SDPG_Foreground);

			if (Bone.PhysicsSettings.Radius > 0)
			{
				DrawWireSphere(PDI, LocationWS, Bone.bDummy ? FColor::Red : FColor::Yellow,
				               Bone.PhysicsSettings.Radius, 16, SDPG_Foreground);
			}

			for (const int32 ChildIndex : Bone.ChildIndices)
			{
				if (Chain.ModifyBones.IsValidIndex(ChildIndex))
				{
					DrawDashedLine(PDI, LocationWS,
					               ComponentTransform.TransformPosition(Chain.ModifyBones[ChildIndex].Location),
					               FLinearColor::White, 1, SDPG_Foreground);
				}
			}
		}
	}
}

void FKawaiiPhysicsEditMode::RenderLimitAngle(FPrimitiveDrawInterface* PDI) const
{
	if (!GraphNode->bEnableDebugDrawLimitAngle || !GEngine->ConstraintLimitMaterialPrismatic)
	{
		return;
	}

	const FTransform ComponentTransform = GetPreviewComponentTransform(GetAnimPreviewScene());
	for (const FKawaiiPhysicsChain& Chain : RuntimeNode->Chains)
	{
		for (const FKawaiiPhysicsModifyBone& Bone : Chain.ModifyBones)
		{
			if (!Bone.IsSimulated() || Bone.PhysicsSettings.LimitAngle <= 0.0f ||
				!Chain.ModifyBones.IsValidIndex(Bone.ParentIndex))
			{
				continue;
			}

			const FKawaiiPhysicsModifyBone& Parent = Chain.ModifyBones[Bone.ParentIndex];
			const FVector ParentWS = ComponentTransform.TransformPosition(Parent.PrevLocation);
			const FVector BoneWS = ComponentTransform.TransformPosition(Bone.PrevLocation);
			const float Angle = FMath::DegreesToRadians(Bone.PhysicsSettings.LimitAngle);

			DrawCone(PDI, FScaleMatrix(5.0f) * FTransform((BoneWS - ParentWS).Rotation(), ParentWS).ToMatrixNoScale(),
			         Angle, Angle, 24, true, FLinearColor::White,
			         GEngine->ConstraintLimitMaterialPrismatic->GetRenderProxy(), SDPG_World);
		}
	}
}

void FKawaiiPhysicsEditMode::RenderSphericalLimits(FPrimitiveDrawInterface* PDI) const
{
	if (!GraphNode->bEnableDebugDrawSphereLimit)
	{
		return;
	}

	const FTransform ComponentTransform = GetPreviewComponentTransform(GetAnimPreviewScene());
	auto Draw = [&](const FSphericalLimit& Sphere, int32 Index)
	{
		if (!Sphere.bEnable || Sphere.Radius <= 0)
		{
			return;
		}

		const FMaterialRenderProxy* Material = GetLimitMaterial(Sphere.SourceType);
		const bool bSelectable = Sphere.SourceType != ECollisionSourceType::PhysicsAsset;
		const FVector LocationWS = ComponentTransform.TransformPosition(Sphere.Location);
		const FRotator RotationWS = (ComponentTransform.GetRotation() * Sphere.Rotation).Rotator();

		PDI->SetHitProxy(bSelectable
			                 ? new HKawaiiPhysicsHitProxy(ECollisionLimitType::Spherical, Index, Sphere.SourceType)
			                 : nullptr);
		if (Material)
		{
			DrawSphere(PDI, LocationWS, FRotator::ZeroRotator, FVector(Sphere.Radius), 24, 6, Material, SDPG_World);
		}
		DrawWireSphere(PDI, LocationWS, FLinearColor::Black, Sphere.Radius, 24, SDPG_World);
		DrawCoordinateSystem(PDI, LocationWS, RotationWS, Sphere.Radius, SDPG_World + 1);
		PDI->SetHitProxy(nullptr);
	};

	for (int32 i = 0; i < RuntimeNode->SphericalLimits.Num(); ++i)
	{
		Draw(RuntimeNode->SphericalLimits[i], i);
	}
	for (int32 i = 0; i < RuntimeNode->SphericalLimitsData.Num(); ++i)
	{
		Draw(RuntimeNode->SphericalLimitsData[i], i);
	}
}

void FKawaiiPhysicsEditMode::RenderCapsuleLimits(FPrimitiveDrawInterface* PDI) const
{
	if (!GraphNode->bEnableDebugDrawCapsuleLimit)
	{
		return;
	}

	const FTransform ComponentTransform = GetPreviewComponentTransform(GetAnimPreviewScene());
	auto Draw = [&](const FCapsuleLimit& Capsule, int32 Index)
	{
		if (!Capsule.bEnable || Capsule.Radius <= 0 || Capsule.Length <= 0)
		{
			return;
		}

		const FMaterialRenderProxy* Material = GetLimitMaterial(Capsule.SourceType);
		const bool bSelectable = Capsule.SourceType != ECollisionSourceType::PhysicsAsset;
		const FVector LocationWS = ComponentTransform.TransformPosition(Capsule.Location);
		const FQuat RotationWS = ComponentTransform.GetRotation() * Capsule.Rotation;
		const FVector XAxis = RotationWS.GetAxisX();
		const FVector YAxis = RotationWS.GetAxisY();
		const FVector ZAxis = RotationWS.GetAxisZ();
		const float HalfLength = 0.5f * Capsule.Length;

		PDI->SetHitProxy(bSelectable
			                 ? new HKawaiiPhysicsHitProxy(ECollisionLimitType::Capsule, Index, Capsule.SourceType)
			                 : nullptr);
		if (Material)
		{
			// A capsule is a cylinder capped by two half-spheres; the length is
			// the cylindrical section only.
			DrawCylinder(PDI, LocationWS, XAxis, YAxis, ZAxis, Capsule.Radius, HalfLength, 25, Material, SDPG_World);
			DrawSphere(PDI, LocationWS + ZAxis * HalfLength, RotationWS.Rotator(), FVector(Capsule.Radius), 24, 6,
			           Material, SDPG_World);
			DrawSphere(PDI, LocationWS - ZAxis * HalfLength, RotationWS.Rotator(), FVector(Capsule.Radius), 24, 6,
			           Material, SDPG_World);
		}
		DrawWireCapsule(PDI, LocationWS, XAxis, YAxis, ZAxis, FLinearColor::Black, Capsule.Radius,
		                HalfLength + Capsule.Radius, 25, SDPG_World);
		DrawCoordinateSystem(PDI, LocationWS, RotationWS.Rotator(), Capsule.Radius, SDPG_World + 1);
		PDI->SetHitProxy(nullptr);
	};

	for (int32 i = 0; i < RuntimeNode->CapsuleLimits.Num(); ++i)
	{
		Draw(RuntimeNode->CapsuleLimits[i], i);
	}
	for (int32 i = 0; i < RuntimeNode->CapsuleLimitsData.Num(); ++i)
	{
		Draw(RuntimeNode->CapsuleLimitsData[i], i);
	}
}

void FKawaiiPhysicsEditMode::RenderBoxLimits(FPrimitiveDrawInterface* PDI) const
{
	if (!GraphNode->bEnableDebugDrawBoxLimit)
	{
		return;
	}

	const FTransform ComponentTransform = GetPreviewComponentTransform(GetAnimPreviewScene());
	auto Draw = [&](const FBoxLimit& Box, int32 Index)
	{
		if (!Box.bEnable || Box.Extent.Size() <= 0)
		{
			return;
		}

		const FMaterialRenderProxy* Material = GetLimitMaterial(Box.SourceType);
		const bool bSelectable = Box.SourceType != ECollisionSourceType::PhysicsAsset;
		const FTransform BoxTransformWS = FTransform(Box.Rotation, Box.Location) * ComponentTransform;

		PDI->SetHitProxy(bSelectable
			                 ? new HKawaiiPhysicsHitProxy(ECollisionLimitType::Box, Index, Box.SourceType)
			                 : nullptr);
		if (Material)
		{
			DrawBox(PDI, BoxTransformWS.ToMatrixWithScale(), Box.Extent, Material, SDPG_World);
		}
		DrawWireBox(PDI, BoxTransformWS.ToMatrixWithScale(), FBox(-Box.Extent, Box.Extent), FLinearColor::Black,
		            SDPG_World);
		DrawCoordinateSystem(PDI, BoxTransformWS.GetLocation(), BoxTransformWS.Rotator(), Box.Extent.Size(),
		                     SDPG_World + 1);
		PDI->SetHitProxy(nullptr);
	};

	for (int32 i = 0; i < RuntimeNode->BoxLimits.Num(); ++i)
	{
		Draw(RuntimeNode->BoxLimits[i], i);
	}
	for (int32 i = 0; i < RuntimeNode->BoxLimitsData.Num(); ++i)
	{
		Draw(RuntimeNode->BoxLimitsData[i], i);
	}
}

void FKawaiiPhysicsEditMode::RenderPlanarLimits(FPrimitiveDrawInterface* PDI) const
{
	if (!GraphNode->bEnableDebugDrawPlanerLimit)
	{
		return;
	}

	const FTransform ComponentTransform = GetPreviewComponentTransform(GetAnimPreviewScene());
	auto Draw = [&](const FPlanarLimit& Plane, int32 Index)
	{
		FTransform PlaneTransformWS = FTransform(Plane.Rotation, Plane.Location) * ComponentTransform;
		PlaneTransformWS.NormalizeRotation();

		const FMaterialRenderProxy* Material = GetLimitMaterial(Plane.SourceType);
		const bool bSelectable = Plane.SourceType != ECollisionSourceType::PhysicsAsset;

		PDI->SetHitProxy(bSelectable
			                 ? new HKawaiiPhysicsHitProxy(ECollisionLimitType::Planar, Index, Plane.SourceType)
			                 : nullptr);
		if (Material)
		{
			DrawPlane10x10(PDI, PlaneTransformWS.ToMatrixWithScale(), 200.0f, FVector2D(0.0f, 0.0f),
			               FVector2D(1.0f, 1.0f), Material, SDPG_World);
		}
		// Arrow along the plane normal, so the side that pushes is obvious.
		DrawDirectionalArrow(PDI, FRotationMatrix(FRotator(90.0f, 0.0f, 0.0f)) * PlaneTransformWS.ToMatrixWithScale(),
		                     FLinearColor::Blue, 50.0f, 20.0f, SDPG_Foreground, 0.5f);
		PDI->SetHitProxy(nullptr);
	};

	for (int32 i = 0; i < RuntimeNode->PlanarLimits.Num(); ++i)
	{
		Draw(RuntimeNode->PlanarLimits[i], i);
	}
	for (int32 i = 0; i < RuntimeNode->PlanarLimitsData.Num(); ++i)
	{
		Draw(RuntimeNode->PlanarLimitsData[i], i);
	}
}

void FKawaiiPhysicsEditMode::RenderBoneConstraints(FPrimitiveDrawInterface* PDI) const
{
	if (!GraphNode->bEnableDebugDrawBoneConstraint)
	{
		return;
	}

	const FTransform ComponentTransform = GetPreviewComponentTransform(GetAnimPreviewScene());
	for (const FKawaiiPhysicsChain& Chain : RuntimeNode->Chains)
	{
		for (const FModifyBoneConstraint& Constraint : Chain.BoneConstraintSettings.MergedBoneConstraints)
		{
			if (!Constraint.IsBoneReferenceValid() ||
				!Chain.ModifyBones.IsValidIndex(Constraint.ModifyBoneIndex1) ||
				!Chain.ModifyBones.IsValidIndex(Constraint.ModifyBoneIndex2))
			{
				continue;
			}

			const FVector Location1 = ComponentTransform.TransformPosition(
				Chain.ModifyBones[Constraint.ModifyBoneIndex1].PrevLocation);
			const FVector Location2 = ComponentTransform.TransformPosition(
				Chain.ModifyBones[Constraint.ModifyBoneIndex2].PrevLocation);

			const FVector Direction = (Location2 - Location1).GetSafeNormal();
			const float Distance = (Location2 - Location1).Size();

			// Drawn both ways so the pair reads as a link, not a direction.
			DrawDirectionalArrow(PDI, FTransform(FRotationMatrix::MakeFromX(Direction).Rotator(), Location1)
			                     .ToMatrixNoScale(), FLinearColor::Red, Distance, 1, SDPG_Foreground);
			DrawDirectionalArrow(PDI, FTransform(FRotationMatrix::MakeFromX(-Direction).Rotator(), Location2)
			                     .ToMatrixNoScale(), FLinearColor::Red, Distance, 1, SDPG_Foreground);
		}
	}
}

void FKawaiiPhysicsEditMode::RenderExternalForces(FPrimitiveDrawInterface* PDI) const
{
	if (!GraphNode->bEnableDebugDrawExternalForce)
	{
		return;
	}

	for (FKawaiiPhysicsChain& Chain : RuntimeNode->Chains)
	{
		for (const FKawaiiPhysicsModifyBone& Bone : Chain.ModifyBones)
		{
			for (FInstancedStruct& ForceStruct : Chain.ExternalForceSettings.ExternalForces)
			{
				if (FKawaiiPhysics_ExternalForce* Force = ForceStruct.GetMutablePtr<FKawaiiPhysics_ExternalForce>())
				{
					Force->AnimDrawDebugForEditMode(Bone, *RuntimeNode, PDI);
				}
			}
		}
	}
}

///////////////////////////////////////////////////////////////////////////////
// Selection and gizmo
///////////////////////////////////////////////////////////////////////////////

FVector FKawaiiPhysicsEditMode::GetWidgetLocation() const
{
	const FTransform ComponentTransform = GetPreviewComponentTransform(GetAnimPreviewScene());
	if (const FCollisionLimitBase* Collision = GetSelectCollisionLimitRuntime())
	{
		return ComponentTransform.TransformPosition(Collision->Location);
	}
	return ComponentTransform.GetLocation();
}

bool FKawaiiPhysicsEditMode::GetCustomDrawingCoordinateSystem(FMatrix& InMatrix, void* InData)
{
	const FCollisionLimitBase* Collision = GetSelectCollisionLimitRuntime();
	if (!Collision)
	{
		return false;
	}

	const FTransform ComponentTransform = GetPreviewComponentTransform(GetAnimPreviewScene());
	InMatrix = FTransform(ComponentTransform.GetRotation() * Collision->Rotation).ToMatrixNoScale();
	return true;
}

UE::Widget::EWidgetMode FKawaiiPhysicsEditMode::GetWidgetMode() const
{
	if (GetSelectCollisionLimitRuntime())
	{
		if (CurWidgetMode == UE::Widget::EWidgetMode::WM_None)
		{
			CurWidgetMode = UE::Widget::EWidgetMode::WM_Translate;
		}
		return CurWidgetMode;
	}

	return UE::Widget::EWidgetMode::WM_Translate;
}

UE::Widget::EWidgetMode FKawaiiPhysicsEditMode::NextWidgetMode(UE::Widget::EWidgetMode InWidgetMode)
{
	switch (InWidgetMode)
	{
	case UE::Widget::EWidgetMode::WM_Translate: return UE::Widget::EWidgetMode::WM_Rotate;
	case UE::Widget::EWidgetMode::WM_Rotate: return UE::Widget::EWidgetMode::WM_Scale;
	default: return UE::Widget::EWidgetMode::WM_Translate;
	}
}

ECoordSystem FKawaiiPhysicsEditMode::GetWidgetCoordinateSystem() const
{
	return COORD_Local;
}

bool FKawaiiPhysicsEditMode::ShouldDrawWidget() const
{
	return IsValidSelectCollision();
}

bool FKawaiiPhysicsEditMode::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy,
                                         const FViewportClick& Click)
{
	bool bResult = FAnimNodeEditMode::HandleClick(InViewportClient, HitProxy, Click);

	if (HitProxy && HitProxy->IsA(HKawaiiPhysicsHitProxy::StaticGetType()))
	{
		const HKawaiiPhysicsHitProxy* KawaiiHitProxy = static_cast<HKawaiiPhysicsHitProxy*>(HitProxy);
		SelectCollisionType = KawaiiHitProxy->CollisionType;
		SelectCollisionIndex = KawaiiHitProxy->CollisionIndex;
		SelectCollisionSourceType = KawaiiHitProxy->SourceType;
		bResult = true;
	}
	else
	{
		SelectCollisionType = ECollisionLimitType::None;
		SelectCollisionIndex = INDEX_NONE;
	}

	return bResult;
}

bool FKawaiiPhysicsEditMode::InputKey(FEditorViewportClient* InViewportClient, FViewport* InViewport, FKey InKey,
                                      EInputEvent InEvent)
{
	if (InEvent != IE_Pressed || IsManipulatingWidget())
	{
		return false;
	}

	if (InKey == EKeys::SpaceBar)
	{
		CurWidgetMode = NextWidgetMode(GetWidgetMode());
		GetModeManager()->SetWidgetMode(CurWidgetMode);
		InViewportClient->Invalidate();
		return true;
	}

	if (InKey == EKeys::Q)
	{
		const ECoordSystem CoordSystem = GetModeManager()->GetCoordSystem();
		GetModeManager()->SetCoordSystem(CoordSystem == COORD_Local ? COORD_World : COORD_Local);
		return false;
	}

	if (InKey == EKeys::Delete && SelectCollisionSourceType != ECollisionSourceType::PhysicsAsset &&
		IsValidSelectCollision())
	{
		const bool bFromDataAsset = SelectCollisionSourceType == ECollisionSourceType::DataAsset;
		UKawaiiPhysicsLimitsDataAsset* Asset = RuntimeNode->LimitsDataAsset;

		auto RemoveFrom = [&](auto& RuntimeArray, auto& GraphArray, auto& AssetArray)
		{
			if (bFromDataAsset)
			{
				if (Asset && AssetArray.IsValidIndex(SelectCollisionIndex))
				{
					AssetArray.RemoveAt(SelectCollisionIndex);
					Asset->MarkPackageDirty();
				}
			}
			else
			{
				if (RuntimeArray.IsValidIndex(SelectCollisionIndex))
				{
					RuntimeArray.RemoveAt(SelectCollisionIndex);
				}
				if (GraphArray.IsValidIndex(SelectCollisionIndex))
				{
					GraphArray.RemoveAt(SelectCollisionIndex);
				}
			}
		};

		// The data asset arrays are only touched when the selection came from
		// it, so a null asset is never dereferenced.
		TArray<FSphericalLimit> NoSpheres;
		TArray<FCapsuleLimit> NoCapsules;
		TArray<FBoxLimit> NoBoxes;
		TArray<FPlanarLimit> NoPlanes;

		switch (SelectCollisionType)
		{
		case ECollisionLimitType::Spherical:
			RemoveFrom(RuntimeNode->SphericalLimits, GraphNode->Node.SphericalLimits,
			           Asset ? Asset->SphericalLimits : NoSpheres);
			break;
		case ECollisionLimitType::Capsule:
			RemoveFrom(RuntimeNode->CapsuleLimits, GraphNode->Node.CapsuleLimits,
			           Asset ? Asset->CapsuleLimits : NoCapsules);
			break;
		case ECollisionLimitType::Box:
			RemoveFrom(RuntimeNode->BoxLimits, GraphNode->Node.BoxLimits, Asset ? Asset->BoxLimits : NoBoxes);
			break;
		case ECollisionLimitType::Planar:
			RemoveFrom(RuntimeNode->PlanarLimits, GraphNode->Node.PlanarLimits, Asset ? Asset->PlanarLimits : NoPlanes);
			break;
		default:
			break;
		}

		SelectCollisionType = ECollisionLimitType::None;
		SelectCollisionIndex = INDEX_NONE;
		return true;
	}

	return false;
}

void FKawaiiPhysicsEditMode::OnExternalNodePropertyChange(FPropertyChangedEvent& InPropertyEvent)
{
	if (!IsValidSelectCollision())
	{
		SelectCollisionIndex = INDEX_NONE;
		SelectCollisionType = ECollisionLimitType::None;
		CurWidgetMode = UE::Widget::EWidgetMode::WM_None;
	}

	if (InPropertyEvent.Property &&
		InPropertyEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, LimitsDataAsset))
	{
		BindLimitsDataAsset();
	}
}

void FKawaiiPhysicsEditMode::OnLimitDataAssetPropertyChange(FPropertyChangedEvent& InPropertyEvent)
{
	if (!GraphNode || !RuntimeNode)
	{
		return;
	}

	GraphNode->Node.SphericalLimitsData = RuntimeNode->SphericalLimitsData;
	GraphNode->Node.CapsuleLimitsData = RuntimeNode->CapsuleLimitsData;
	GraphNode->Node.BoxLimitsData = RuntimeNode->BoxLimitsData;
	GraphNode->Node.PlanarLimitsData = RuntimeNode->PlanarLimitsData;
}

bool FKawaiiPhysicsEditMode::IsSelectAnimNodeCollision() const
{
	return SelectCollisionSourceType == ECollisionSourceType::AnimNode;
}

bool FKawaiiPhysicsEditMode::IsValidSelectCollision() const
{
	return GetSelectCollisionLimitRuntime() != nullptr;
}

FCollisionLimitBase* FKawaiiPhysicsEditMode::GetSelectCollisionLimitRuntime() const
{
	if (!RuntimeNode || !GraphNode || SelectCollisionIndex < 0)
	{
		return nullptr;
	}

	auto Pick = [this](auto& NodeArray, auto& DataArray) -> FCollisionLimitBase*
	{
		auto& Array = IsSelectAnimNodeCollision() ? NodeArray : DataArray;
		return Array.IsValidIndex(SelectCollisionIndex) ? &Array[SelectCollisionIndex] : nullptr;
	};

	switch (SelectCollisionType)
	{
	case ECollisionLimitType::Spherical: return Pick(RuntimeNode->SphericalLimits, RuntimeNode->SphericalLimitsData);
	case ECollisionLimitType::Capsule: return Pick(RuntimeNode->CapsuleLimits, RuntimeNode->CapsuleLimitsData);
	case ECollisionLimitType::Box: return Pick(RuntimeNode->BoxLimits, RuntimeNode->BoxLimitsData);
	case ECollisionLimitType::Planar: return Pick(RuntimeNode->PlanarLimits, RuntimeNode->PlanarLimitsData);
	default: return nullptr;
	}
}

FCollisionLimitBase* FKawaiiPhysicsEditMode::GetSelectCollisionLimitGraph() const
{
	if (!RuntimeNode || !GraphNode || SelectCollisionIndex < 0)
	{
		return nullptr;
	}

	FAnimNode_KawaiiPhysics& Node = GraphNode->Node;
	auto Pick = [this](auto& NodeArray, auto& DataArray) -> FCollisionLimitBase*
	{
		auto& Array = IsSelectAnimNodeCollision() ? NodeArray : DataArray;
		return Array.IsValidIndex(SelectCollisionIndex) ? &Array[SelectCollisionIndex] : nullptr;
	};

	switch (SelectCollisionType)
	{
	case ECollisionLimitType::Spherical: return Pick(Node.SphericalLimits, Node.SphericalLimitsData);
	case ECollisionLimitType::Capsule: return Pick(Node.CapsuleLimits, Node.CapsuleLimitsData);
	case ECollisionLimitType::Box: return Pick(Node.BoxLimits, Node.BoxLimitsData);
	case ECollisionLimitType::Planar: return Pick(Node.PlanarLimits, Node.PlanarLimitsData);
	default: return nullptr;
	}
}

void FKawaiiPhysicsEditMode::CommitDataAssetEdit(FCollisionLimitBase* Limit) const
{
	if (SelectCollisionSourceType == ECollisionSourceType::DataAsset && RuntimeNode->LimitsDataAsset)
	{
		RuntimeNode->LimitsDataAsset->UpdateLimit(Limit);
	}
}

void FKawaiiPhysicsEditMode::DoTranslation(FVector& InTranslation)
{
	if (InTranslation.IsNearlyZero())
	{
		return;
	}

	FCollisionLimitBase* CollisionRuntime = GetSelectCollisionLimitRuntime();
	FCollisionLimitBase* CollisionGraph = GetSelectCollisionLimitGraph();
	if (!CollisionRuntime || !CollisionGraph)
	{
		UE_LOG(LogKawaiiPhysicsEd, Warning, TEXT("Kawaii Physics: could not edit the selected limit."));
		return;
	}

	// World-space drag -> component space -> the frame the offset lives in
	// (the driving bone's own space; see the class comment).
	const FTransform ComponentTransform = GetPreviewComponentTransform(GetAnimPreviewScene());
	const FVector DeltaCS = ComponentTransform.InverseTransformVector(InTranslation);
	const FVector DeltaOffset = GetLimitFrameRotation(*CollisionRuntime).UnrotateVector(DeltaCS);

	CollisionRuntime->OffsetLocation += DeltaOffset;
	// Move the resolved location along with it, so the shape tracks the cursor
	// this frame instead of waiting for the next evaluation.
	CollisionRuntime->Location += DeltaCS;
	CollisionGraph->OffsetLocation = CollisionRuntime->OffsetLocation;

	CommitDataAssetEdit(CollisionRuntime);
}

void FKawaiiPhysicsEditMode::DoRotation(FRotator& InRotation)
{
	if (InRotation.IsNearlyZero())
	{
		return;
	}

	FCollisionLimitBase* CollisionRuntime = GetSelectCollisionLimitRuntime();
	FCollisionLimitBase* CollisionGraph = GetSelectCollisionLimitGraph();
	if (!CollisionRuntime || !CollisionGraph)
	{
		UE_LOG(LogKawaiiPhysicsEd, Warning, TEXT("Kawaii Physics: could not edit the selected limit."));
		return;
	}

	// The gizmo's delta is a world-space rotation D. The resolved rotation is
	// Frame * Offset, so the new offset is Frame^-1 * D_cs * Frame * Offset.
	const FTransform ComponentTransform = GetPreviewComponentTransform(GetAnimPreviewScene());
	const FQuat ComponentRotation = ComponentTransform.GetRotation();
	const FQuat DeltaCS = ComponentRotation.Inverse() * InRotation.Quaternion() * ComponentRotation;

	const FQuat FrameRotation = GetLimitFrameRotation(*CollisionRuntime);
	const FQuat NewResolved = DeltaCS * CollisionRuntime->Rotation;
	const FQuat NewOffset = FrameRotation.Inverse() * NewResolved;

	CollisionRuntime->OffsetRotation = FRotator(NewOffset);
	CollisionRuntime->Rotation = NewResolved;
	CollisionGraph->OffsetRotation = CollisionRuntime->OffsetRotation;

	CommitDataAssetEdit(CollisionRuntime);
}

void FKawaiiPhysicsEditMode::DoScale(FVector& InScale)
{
	if (InScale.IsNearlyZero() || SelectCollisionType == ECollisionLimitType::Planar)
	{
		return;
	}

	FCollisionLimitBase* CollisionRuntime = GetSelectCollisionLimitRuntime();
	FCollisionLimitBase* CollisionGraph = GetSelectCollisionLimitGraph();
	if (!CollisionRuntime || !CollisionGraph)
	{
		UE_LOG(LogKawaiiPhysicsEd, Warning, TEXT("Kawaii Physics: could not edit the selected limit."));
		return;
	}

	// As upstream: any axis grows a sphere; X/Y grow a capsule's radius and Z
	// its length; a box scales per axis.
	if (SelectCollisionType == ECollisionLimitType::Spherical)
	{
		FSphericalLimit& Runtime = *static_cast<FSphericalLimit*>(CollisionRuntime);
		Runtime.Radius = FMath::Max(Runtime.Radius + InScale.X + InScale.Y + InScale.Z, 0.0f);
		static_cast<FSphericalLimit*>(CollisionGraph)->Radius = Runtime.Radius;
	}
	else if (SelectCollisionType == ECollisionLimitType::Capsule)
	{
		FCapsuleLimit& Runtime = *static_cast<FCapsuleLimit*>(CollisionRuntime);
		Runtime.Radius = FMath::Max(Runtime.Radius + InScale.X + InScale.Y, 0.0f);
		Runtime.Length = FMath::Max(Runtime.Length + InScale.Z, 0.0f);

		FCapsuleLimit& Graph = *static_cast<FCapsuleLimit*>(CollisionGraph);
		Graph.Radius = Runtime.Radius;
		Graph.Length = Runtime.Length;
	}
	else if (SelectCollisionType == ECollisionLimitType::Box)
	{
		FBoxLimit& Runtime = *static_cast<FBoxLimit*>(CollisionRuntime);
		Runtime.Extent = (Runtime.Extent + InScale).ComponentMax(FVector::ZeroVector);
		static_cast<FBoxLimit*>(CollisionGraph)->Extent = Runtime.Extent;
	}

	CommitDataAssetEdit(CollisionRuntime);
}

///////////////////////////////////////////////////////////////////////////////
// HUD
///////////////////////////////////////////////////////////////////////////////

void FKawaiiPhysicsEditMode::DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport,
                                     const FSceneView* View, FCanvas* Canvas)
{
	if (!RuntimeNode || !GraphNode)
	{
		FAnimNodeEditMode::DrawHUD(ViewportClient, Viewport, View, Canvas);
		return;
	}

	float FontWidth, FontHeight;
	GEngine->GetSmallFont()->GetCharSize(TEXT('L'), FontWidth, FontHeight);
	constexpr float XOffset = 5.0f;
	float DrawPositionY = Viewport->GetSizeXY().Y / Canvas->GetDPIScale() - (3 + FontHeight) -
		100 / Canvas->GetDPIScale();

	if (!FAnimWeight::IsRelevant(RuntimeNode->GetAlpha()) || !RuntimeNode->IsRecentlyEvaluated())
	{
		DrawTextItem(LOCTEXT("NotEvaluated", "This node has not evaluated recently."), Canvas, XOffset,
		             DrawPositionY, FontHeight);
		FAnimNodeEditMode::DrawHUD(ViewportClient, Viewport, View, Canvas);
		return;
	}

	DrawTextItem(LOCTEXT("HelpQ", "Q : Cycle Transform Coordinate System"), Canvas, XOffset, DrawPositionY,
	             FontHeight);
	DrawTextItem(LOCTEXT("HelpSpace", "Space : Cycle Between Translate, Rotate and Scale"), Canvas, XOffset,
	             DrawPositionY, FontHeight);
	DrawTextItem(LOCTEXT("HelpR", "R : Scale Mode"), Canvas, XOffset, DrawPositionY, FontHeight);
	DrawTextItem(LOCTEXT("HelpE", "E : Rotate Mode"), Canvas, XOffset, DrawPositionY, FontHeight);
	DrawTextItem(LOCTEXT("HelpW", "W : Translate Mode"), Canvas, XOffset, DrawPositionY, FontHeight);
	DrawTextItem(LOCTEXT("HelpDelete", "Delete : Remove Selected Limit"), Canvas, XOffset, DrawPositionY, FontHeight);
	DrawTextItem(LOCTEXT("HelpRule", "------------------"), Canvas, XOffset, DrawPositionY, FontHeight);

	FString Selection = TEXT("Select Collision : ");
	switch (SelectCollisionType)
	{
	case ECollisionLimitType::Spherical: Selection += TEXT("Spherical"); break;
	case ECollisionLimitType::Capsule: Selection += TEXT("Capsule"); break;
	case ECollisionLimitType::Box: Selection += TEXT("Box"); break;
	case ECollisionLimitType::Planar: Selection += TEXT("Planar"); break;
	default: Selection += TEXT("None"); break;
	}
	if (SelectCollisionIndex >= 0)
	{
		Selection += FString::Printf(TEXT("[%d]%s"), SelectCollisionIndex,
		                             SelectCollisionSourceType == ECollisionSourceType::DataAsset
			                             ? TEXT(" (Data Asset)")
			                             : TEXT(""));
	}
	DrawTextItem(FText::FromString(Selection), Canvas, XOffset, DrawPositionY, FontHeight);

	const UDebugSkelMeshComponent* PreviewMeshComponent = GetAnimPreviewScene().GetPreviewMeshComponent();
	if (GraphNode->bEnableDebugBoneLengthRate && PreviewMeshComponent && PreviewMeshComponent->MeshObject)
	{
		const FTransform ComponentTransform = PreviewMeshComponent->GetComponentTransform();
		for (const FKawaiiPhysicsChain& Chain : RuntimeNode->Chains)
		{
			for (const FKawaiiPhysicsModifyBone& Bone : Chain.ModifyBones)
			{
				Draw3DTextItem(FText::AsNumber(Bone.LengthRateFromRoot), Canvas, View, Viewport,
				               ComponentTransform.TransformPosition(Bone.Location));
			}
		}
	}

	FAnimNodeEditMode::DrawHUD(ViewportClient, Viewport, View, Canvas);
}

void FKawaiiPhysicsEditMode::DrawTextItem(const FText& Text, FCanvas* Canvas, float X, float& Y, float FontHeight)
{
	FCanvasTextItem TextItem(FVector2D::ZeroVector, Text, GEngine->GetSmallFont(), FLinearColor::White);
	TextItem.EnableShadow(FLinearColor::Black);
	Canvas->DrawItem(TextItem, X, Y);
	Y -= (3 + FontHeight);
}

void FKawaiiPhysicsEditMode::Draw3DTextItem(const FText& Text, FCanvas* Canvas, const FSceneView* View,
                                            const FViewport* Viewport, const FVector& Location)
{
	const int32 HalfX = Viewport->GetSizeXY().X / 2 / Canvas->GetDPIScale();
	const int32 HalfY = Viewport->GetSizeXY().Y / 2 / Canvas->GetDPIScale();

	const FPlane Projection = View->Project(Location);
	if (Projection.W > 0.f)
	{
		const int32 XPos = HalfX + (HalfX * Projection.X);
		const int32 YPos = HalfY + (HalfY * (Projection.Y * -1));
		FCanvasTextItem TextItem(FVector2D(XPos, YPos), Text, GEngine->GetSmallFont(), FLinearColor::White);
		TextItem.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(TextItem);
	}
}

#undef LOCTEXT_NAMESPACE
