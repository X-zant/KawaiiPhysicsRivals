// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License

#pragma once

#include "CoreMinimal.h"
#include "AnimGraphNode_SkeletalControlBase.h"
#include "AnimNode_KawaiiPhysics.h"

#include "AnimGraphNode_KawaiiPhysics.generated.h"

UCLASS(MinimalAPI)
class UAnimGraphNode_KawaiiPhysics : public UAnimGraphNode_SkeletalControlBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = Settings)
	FAnimNode_KawaiiPhysics Node;

	// UEdGraphNode interface
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	// End of UEdGraphNode interface

	// UObject interface
	virtual void PostLoad() override;
	// End of UObject interface

	// UAnimGraphNode_Base interface
	virtual void CopyNodeDataToPreviewNode(FAnimNode_Base* InPreviewNode) override;
	virtual FEditorModeID GetEditorMode() const override;
	virtual void ValidateAnimNodePostCompile(class FCompilerResultsLog& MessageLog,
	                                         class UAnimBlueprintGeneratedClass* CompiledClass,
	                                         int32 CompiledNodeIndex) override;
	// End of UAnimGraphNode_Base interface

protected:
	// UAnimGraphNode_SkeletalControlBase interface
	virtual FText GetControllerDescription() const override;
	virtual const FAnimNode_SkeletalControlBase* GetNode() const override { return &Node; }
	// End of UAnimGraphNode_SkeletalControlBase interface

	/** Index of the chain the edit mode is currently editing. */
	int32 SelectedChainIndex = 0;

	friend class FKawaiiPhysicsEditMode;
};
