// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License

#include "AnimGraphNode_KawaiiPhysics.h"

#include "Kismet2/CompilerResultsLog.h"

#define LOCTEXT_NAMESPACE "KawaiiPhysics"

FText UAnimGraphNode_KawaiiPhysics::GetControllerDescription() const
{
	return LOCTEXT("KawaiiPhysics", "Kawaii Physics");
}

FText UAnimGraphNode_KawaiiPhysics::GetTooltipText() const
{
	return LOCTEXT("KawaiiPhysicsTooltip",
	               "Simple, art-directable secondary motion for bone chains -- hair, skirts, tails and accessories.");
}

FText UAnimGraphNode_KawaiiPhysics::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (TitleType == ENodeTitleType::ListView || TitleType == ENodeTitleType::MenuTitle)
	{
		return GetControllerDescription();
	}

	// Title the node with what it actually drives, so a graph full of these is
	// readable without clicking each one.
	const int32 ChainCount = Node.Chains.Num();

	if (ChainCount == 1)
	{
		const FName RootBone = Node.Chains[0].BoneSettings.RootBone.BoneName;
		if (RootBone != NAME_None)
		{
			FFormatNamedArguments Args;
			Args.Add(TEXT("ControllerDescription"), GetControllerDescription());
			Args.Add(TEXT("RootBone"), FText::FromName(RootBone));
			return FText::Format(LOCTEXT("KawaiiPhysicsTitleSingle", "{ControllerDescription}\nRoot: {RootBone}"),
			                     Args);
		}
	}
	else if (ChainCount > 1)
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("ControllerDescription"), GetControllerDescription());
		Args.Add(TEXT("ChainCount"), FText::AsNumber(ChainCount));
		return FText::Format(LOCTEXT("KawaiiPhysicsTitleMulti", "{ControllerDescription}\n{ChainCount} chains"), Args);
	}

	return GetControllerDescription();
}

void UAnimGraphNode_KawaiiPhysics::PostLoad()
{
	Super::PostLoad();

	// Fold pre-Chains data into Chains[0]. Safe to call repeatedly: it no-ops
	// once a chain has a root bone.
	Node.UpgradeDeprecatedProperties();
}

void UAnimGraphNode_KawaiiPhysics::CopyNodeDataToPreviewNode(FAnimNode_Base* InPreviewNode)
{
	FAnimNode_KawaiiPhysics* PreviewNode = static_cast<FAnimNode_KawaiiPhysics*>(InPreviewNode);
	if (!PreviewNode)
	{
		return;
	}

	// The edit mode manipulates limits on the preview node; mirror them back so
	// gizmo edits survive and are visible on the authored node.
	PreviewNode->SphericalLimits = Node.SphericalLimits;
	PreviewNode->CapsuleLimits = Node.CapsuleLimits;
	PreviewNode->BoxLimits = Node.BoxLimits;
	PreviewNode->PlanarLimits = Node.PlanarLimits;
}

void UAnimGraphNode_KawaiiPhysics::ValidateAnimNodePostCompile(FCompilerResultsLog& MessageLog,
                                                               UAnimBlueprintGeneratedClass* CompiledClass,
                                                               int32 CompiledNodeIndex)
{
	Super::ValidateAnimNodePostCompile(MessageLog, CompiledClass, CompiledNodeIndex);

	if (Node.Chains.IsEmpty())
	{
		MessageLog.Warning(*LOCTEXT("NoChains", "@@ has no chains and will do nothing.").ToString(), this);
		return;
	}

	for (int32 Index = 0; Index < Node.Chains.Num(); ++Index)
	{
		if (Node.Chains[Index].BoneSettings.RootBone.BoneName == NAME_None)
		{
			MessageLog.Warning(
				*FText::Format(LOCTEXT("NoRootBone", "@@ chain {0} has no Root Bone set."),
				               FText::AsNumber(Index)).ToString(), this);
		}
	}
}

FEditorModeID UAnimGraphNode_KawaiiPhysics::GetEditorMode() const
{
	return "AnimGraph.SkeletalControl.KawaiiPhysics";
}

#undef LOCTEXT_NAMESPACE
