// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License

#include "AnimGraphNode_KawaiiPhysicsMerge.h"

#include "Kismet2/CompilerResultsLog.h"

#define LOCTEXT_NAMESPACE "KawaiiPhysics"

FText UAnimGraphNode_KawaiiPhysicsMerge::GetControllerDescription() const
{
	return LOCTEXT("KawaiiPhysicsMerge", "Kawaii Physics (Merge)");
}

FText UAnimGraphNode_KawaiiPhysicsMerge::GetTooltipText() const
{
	return LOCTEXT("KawaiiPhysicsMergeTooltip",
	               "Simulates several bone chains against one shared set of collision limits and one shared "
	               "constraint solve. Cheaper than stacking a Kawaii Physics node per chain.");
}

FText UAnimGraphNode_KawaiiPhysicsMerge::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (TitleType == ENodeTitleType::ListView || TitleType == ENodeTitleType::MenuTitle)
	{
		return GetControllerDescription();
	}

	FFormatNamedArguments Args;
	Args.Add(TEXT("ControllerDescription"), GetControllerDescription());
	Args.Add(TEXT("ChainCount"), FText::AsNumber(Node.Nodes.Num()));
	return FText::Format(LOCTEXT("KawaiiPhysicsMergeTitle", "{ControllerDescription}\n{ChainCount} chains"), Args);
}

void UAnimGraphNode_KawaiiPhysicsMerge::ValidateAnimNodePostCompile(FCompilerResultsLog& MessageLog,
                                                                    UAnimBlueprintGeneratedClass* CompiledClass,
                                                                    int32 CompiledNodeIndex)
{
	Super::ValidateAnimNodePostCompile(MessageLog, CompiledClass, CompiledNodeIndex);

	if (Node.Nodes.IsEmpty())
	{
		MessageLog.Warning(*LOCTEXT("NoMergeChains", "@@ has no chains and will do nothing.").ToString(), this);
	}

	if (!Node.ExternalForces.IsEmpty())
	{
		// Serialized for schema compatibility, but this implementation only
		// applies the node's own Gravity and Wind. Say so rather than let it
		// silently do nothing.
		MessageLog.Note(
			*LOCTEXT("MergeExternalForces",
			         "@@ has External Force presets. The Merge node applies only its own Gravity and Wind; "
			         "use a regular Kawaii Physics node for force presets.").ToString(), this);
	}
}

#undef LOCTEXT_NAMESPACE
