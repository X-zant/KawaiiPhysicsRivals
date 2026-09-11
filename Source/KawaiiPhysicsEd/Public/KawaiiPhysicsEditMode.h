// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License

#pragma once

#include "CoreMinimal.h"
#include "AnimNodeEditMode.h"

class UAnimGraphNode_KawaiiPhysics;
struct FAnimNode_KawaiiPhysics;

/**
 * Viewport visualisation for a Kawaii Physics node.
 *
 * Draws the simulated bones and every collision limit the node resolves, which
 * is the only reliable way to see why a chain is being pushed where it is.
 * Deliberately read-only: limits are edited through the details panel, so there
 * is no gizmo state here to get out of sync with the authored node.
 */
class FKawaiiPhysicsEditMode : public FAnimNodeEditMode
{
public:
	// IAnimNodeEditMode interface
	virtual void EnterMode(UAnimGraphNode_Base* InEditorNode, FAnimNode_Base* InRuntimeNode) override;
	virtual void ExitMode() override;
	// End of IAnimNodeEditMode interface

	// FEdMode interface
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;
	virtual bool ShouldDrawWidget() const override { return false; }
	virtual UE::Widget::EWidgetMode GetWidgetMode() const override { return UE::Widget::EWidgetMode::WM_None; }
	// End of FEdMode interface

private:
	void RenderChains(FPrimitiveDrawInterface* PDI, const FTransform& ComponentTransform) const;
	void RenderLimits(FPrimitiveDrawInterface* PDI, const FTransform& ComponentTransform) const;

	UAnimGraphNode_KawaiiPhysics* GraphNode = nullptr;
	FAnimNode_KawaiiPhysics* RuntimeNode = nullptr;
};
