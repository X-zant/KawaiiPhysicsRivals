// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License

#pragma once

#include "CoreMinimal.h"
#include "AnimNodeEditMode.h"
#include "AnimNode_KawaiiPhysics.h"

class FCanvas;
class FEditorViewportClient;
class FPrimitiveDrawInterface;
class UAnimGraphNode_KawaiiPhysics;
class UMaterialInstanceDynamic;
struct FViewportClick;

/**
 * Viewport editing for a Kawaii Physics node, ported from upstream v1.18.0.
 *
 * Collision limits draw as translucent solids that can be clicked to select
 * and then moved, rotated and scaled with the gizmo (W/E/R, Space to cycle,
 * Q to toggle local/world, Delete to remove). Bones, angle limits, bone
 * constraints and external forces are drawn too, each switchable from the
 * node's "Debug Visualization" row.
 *
 * Differences from upstream, both forced by the fork:
 *  - Bones, constraints and forces live per chain, so they are drawn for
 *    every chain.
 *  - The gizmo edits offsets in the DRIVING BONE's own space, not its
 *    parent's. That is how the game resolves limits (verified against stored
 *    limit locations in cooked assets), so a drag moves the shape exactly
 *    under the cursor.
 */
class FKawaiiPhysicsEditMode : public FAnimNodeEditMode
{
public:
	FKawaiiPhysicsEditMode();

	// IAnimNodeEditMode interface
	virtual void EnterMode(UAnimGraphNode_Base* InEditorNode, FAnimNode_Base* InRuntimeNode) override;
	virtual void ExitMode() override;
	virtual FVector GetWidgetLocation() const override;
	virtual UE::Widget::EWidgetMode GetWidgetMode() const override;
	virtual ECoordSystem GetWidgetCoordinateSystem() const override;
	virtual void DoTranslation(FVector& InTranslation) override;
	virtual void DoRotation(FRotator& InRotation) override;
	virtual void DoScale(FVector& InScale) override;
	// End of IAnimNodeEditMode interface

	// FEdMode interface
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;
	virtual bool HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy,
	                         const FViewportClick& Click) override;
	virtual bool GetCustomDrawingCoordinateSystem(FMatrix& InMatrix, void* InData) override;
	virtual bool InputKey(FEditorViewportClient* InViewportClient, FViewport* InViewport, FKey InKey,
	                      EInputEvent InEvent) override;
	virtual bool ShouldDrawWidget() const override;
	virtual void DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View,
	                     FCanvas* Canvas) override;
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	// End of FEdMode interface

private:
	void OnExternalNodePropertyChange(FPropertyChangedEvent& InPropertyEvent);
	void OnLimitDataAssetPropertyChange(FPropertyChangedEvent& InPropertyEvent);
	void BindLimitsDataAsset();
	void UnbindLimitsDataAsset();

	void RenderModifyBones(FPrimitiveDrawInterface* PDI) const;
	void RenderLimitAngle(FPrimitiveDrawInterface* PDI) const;
	void RenderSphericalLimits(FPrimitiveDrawInterface* PDI) const;
	void RenderCapsuleLimits(FPrimitiveDrawInterface* PDI) const;
	void RenderBoxLimits(FPrimitiveDrawInterface* PDI) const;
	void RenderPlanarLimits(FPrimitiveDrawInterface* PDI) const;
	void RenderBoneConstraints(FPrimitiveDrawInterface* PDI) const;
	void RenderExternalForces(FPrimitiveDrawInterface* PDI) const;

	/** Material for a limit by where it came from; null means "draw wireframe only". */
	const FMaterialRenderProxy* GetLimitMaterial(ECollisionSourceType SourceType) const;

	/** Gizmo cycles Translate -> Rotate -> Scale. */
	static UE::Widget::EWidgetMode NextWidgetMode(UE::Widget::EWidgetMode InWidgetMode);

	bool IsSelectAnimNodeCollision() const;
	bool IsValidSelectCollision() const;
	FCollisionLimitBase* GetSelectCollisionLimitRuntime() const;
	FCollisionLimitBase* GetSelectCollisionLimitGraph() const;

	/** Pushes an edited data-asset limit back into the asset. */
	void CommitDataAssetEdit(FCollisionLimitBase* Limit) const;

	static void DrawTextItem(const FText& Text, FCanvas* Canvas, float X, float& Y, float FontHeight);
	static void Draw3DTextItem(const FText& Text, FCanvas* Canvas, const FSceneView* View, const FViewport* Viewport,
	                           const FVector& Location);

	FAnimNode_KawaiiPhysics* RuntimeNode = nullptr;
	UAnimGraphNode_KawaiiPhysics* GraphNode = nullptr;

	ECollisionLimitType SelectCollisionType = ECollisionLimitType::None;
	int32 SelectCollisionIndex = INDEX_NONE;
	ECollisionSourceType SelectCollisionSourceType = ECollisionSourceType::AnimNode;

	mutable UE::Widget::EWidgetMode CurWidgetMode = UE::Widget::EWidgetMode::WM_Translate;

	FDelegateHandle NodePropertyDelegateHandle;
	FDelegateHandle LimitsDataAssetPropertyDelegateHandle;
	TWeakObjectPtr<UKawaiiPhysicsLimitsDataAsset> BoundLimitsDataAsset;

	/** Translucent material for limits generated from a physics asset (not editable). */
	TObjectPtr<UMaterialInstanceDynamic> PhysicsAssetBodyMaterial = nullptr;
};
