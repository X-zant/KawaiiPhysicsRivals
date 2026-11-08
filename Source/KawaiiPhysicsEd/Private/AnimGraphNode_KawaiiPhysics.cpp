// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License

#include "AnimGraphNode_KawaiiPhysics.h"

#include "AssetToolsModule.h"
#include "DetailCategoryBuilder.h"
#include "Editor.h"
#include "IAssetTools.h"
#include "KawaiiPhysicsBoneConstraintsDataAsset.h"
#include "KawaiiPhysicsLimitsDataAsset.h"
#include "Selection.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dialogs/DlgPickAssetPath.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Text/STextBlock.h"

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

	// Push every authored setting to the preview instance, for live editing --
	// as upstream does. Copied field by field rather than wholesale: the base
	// node's pose links must not be overwritten on the running preview.
	PreviewNode->Chains = Node.Chains;
	PreviewNode->TargetFramerate = Node.TargetFramerate;
	PreviewNode->OverrideTargetFramerate = Node.OverrideTargetFramerate;
	PreviewNode->WarmUpFrames = Node.WarmUpFrames;
	PreviewNode->bUseWarmUpWhenResetDynamics = Node.bUseWarmUpWhenResetDynamics;
	PreviewNode->bNeedWarmUp = Node.bNeedWarmUp;
	PreviewNode->bUpdatePhysicsSettingsInGame = Node.bUpdatePhysicsSettingsInGame;
	PreviewNode->ResetBoneTransformWhenBoneNotFound = Node.ResetBoneTransformWhenBoneNotFound;

	PreviewNode->SphericalLimits = Node.SphericalLimits;
	PreviewNode->CapsuleLimits = Node.CapsuleLimits;
	PreviewNode->BoxLimits = Node.BoxLimits;
	PreviewNode->PlanarLimits = Node.PlanarLimits;
	PreviewNode->LimitsDataAsset = Node.LimitsDataAsset;
	PreviewNode->PhysicsAssetForLimits = Node.PhysicsAssetForLimits;

	PreviewNode->bAllowWorldCollision = Node.bAllowWorldCollision;
	PreviewNode->bOverrideCollisionParams = Node.bOverrideCollisionParams;
	PreviewNode->CollisionChannelSettings = Node.CollisionChannelSettings;
	PreviewNode->bIgnoreSelfComponent = Node.bIgnoreSelfComponent;
	PreviewNode->IgnoreBones = Node.IgnoreBones;
	PreviewNode->IgnoreBoneNamePrefix = Node.IgnoreBoneNamePrefix;

	PreviewNode->KawaiiPhysicsTag = Node.KawaiiPhysicsTag;

	// Rebuild the simulation from the new settings, as upstream does on sync.
	// Re-initialising a chain also re-derives its per-bone physics settings.
	for (FKawaiiPhysicsChain& Chain : PreviewNode->Chains)
	{
		Chain.ModifyBones.Empty();
		Chain.bInitialized = false;
	}
}

void UAnimGraphNode_KawaiiPhysics::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// As upstream: drop the simulation state and rebuild the node, so exposed
	// pins follow the edit.
	for (FKawaiiPhysicsChain& Chain : Node.Chains)
	{
		Chain.ModifyBones.Empty();
		Chain.bInitialized = false;
	}
	ReconstructNode();
}

void UAnimGraphNode_KawaiiPhysics::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	Super::CustomizeDetails(DetailBuilder);
	CustomizeDetailTools(DetailBuilder);
	CustomizeDetailDebugVisualizations(DetailBuilder);
}

void UAnimGraphNode_KawaiiPhysics::CustomizeDetailTools(IDetailLayoutBuilder& DetailBuilder)
{
	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(TEXT("Kawaii Physics Tools"));
	Category.SetSortOrder(0);

	Category.AddCustomRow(LOCTEXT("KawaiiPhysicsToolsRow", "Kawaii Physics Tools"))
	[
		SNew(SUniformGridPanel)
		.SlotPadding(FMargin(2, 0))
		+ SUniformGridPanel::Slot(0, 0)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked_Lambda([this]()
			{
				ExportLimitsDataAsset();
				return FReply::Handled();
			})
			[
				SNew(STextBlock).Text(LOCTEXT("ExportLimits", "Export Limits"))
			]
		]
		+ SUniformGridPanel::Slot(1, 0)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked_Lambda([this]()
			{
				ExportBoneConstraintsDataAsset();
				return FReply::Handled();
			})
			[
				SNew(STextBlock).Text(LOCTEXT("ExportBoneConstraints", "Export BoneConstraints"))
			]
		]
	];
}

USkeleton* UAnimGraphNode_KawaiiPhysics::GetSkeletonBeingDebugged() const
{
	// The instance being debugged may not be the preview, so ask it directly.
	if (const UAnimBlueprint* AnimBlueprint = GetAnimBlueprint())
	{
		if (const UAnimInstance* Instance = Cast<UAnimInstance>(AnimBlueprint->GetObjectBeingDebugged()))
		{
			return Instance->CurrentSkeleton;
		}
		return AnimBlueprint->TargetSkeleton;
	}
	return nullptr;
}

UPackage* UAnimGraphNode_KawaiiPhysics::CreateDataAssetPackage(const FString& DialogTitle,
                                                               const FString& DefaultSuffix,
                                                               FString& OutAssetName) const
{
	FString DefaultPackageName;
	FString DefaultAssetName;
	const FString AnimBlueprintPath = GetAnimBlueprint()->GetPackage()->GetName();
	FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get().CreateUniqueAssetName(
		AnimBlueprintPath, DefaultSuffix, DefaultPackageName, DefaultAssetName);

	const TSharedRef<SDlgPickAssetPath> Dialog = SNew(SDlgPickAssetPath)
		.Title(FText::FromString(DialogTitle))
		.DefaultAssetPath(FText::FromString(DefaultPackageName));

	if (Dialog->ShowModal() == EAppReturnType::Cancel)
	{
		return nullptr;
	}

	OutAssetName = Dialog->GetAssetName().ToString();
	return CreatePackage(*Dialog->GetFullAssetPath().ToString());
}

void UAnimGraphNode_KawaiiPhysics::ShowExportAssetNotification(UObject* NewAsset, const FText& NotificationText)
{
	FNotificationInfo Info(NotificationText);
	Info.ExpireDuration = 5.0f;
	Info.Hyperlink = FSimpleDelegate::CreateLambda([NewAsset]()
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(NewAsset);
	});
	Info.HyperlinkText = LOCTEXT("OpenCreatedAsset", "Open Created Asset");

	if (const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
	{
		Item->SetCompletionState(SNotificationItem::CS_Success);
	}
}

void UAnimGraphNode_KawaiiPhysics::ExportLimitsDataAsset()
{
	FString AssetName;
	UPackage* Package = CreateDataAssetPackage(TEXT("Choose Location for Collision Data Asset"),
	                                           TEXT("_Collision"), AssetName);
	if (!Package)
	{
		return;
	}

	UKawaiiPhysicsLimitsDataAsset* NewAsset = NewObject<UKawaiiPhysicsLimitsDataAsset>(
		Package, UKawaiiPhysicsLimitsDataAsset::StaticClass(), FName(*AssetName), RF_Public | RF_Standalone);
	if (!NewAsset)
	{
		return;
	}

	NewAsset->Skeleton = GetSkeletonBeingDebugged();

	auto CopyLimits = [](auto& Dest, const auto& Source)
	{
		Dest = Source;
		for (auto& Limit : Dest)
		{
			Limit.SourceType = ECollisionSourceType::DataAsset;
		}
	};
	CopyLimits(NewAsset->SphericalLimits, Node.SphericalLimits);
	CopyLimits(NewAsset->CapsuleLimits, Node.CapsuleLimits);
	CopyLimits(NewAsset->BoxLimits, Node.BoxLimits);
	CopyLimits(NewAsset->PlanarLimits, Node.PlanarLimits);

	USelection* Selection = GEditor->GetSelectedObjects();
	Selection->DeselectAll();
	Selection->Select(NewAsset);

	FAssetRegistryModule::AssetCreated(NewAsset);
	Package->MarkPackageDirty();

	ShowExportAssetNotification(NewAsset, FText::Format(
		LOCTEXT("ExportedLimitsDataAsset", "Exported Limits Data Asset: {0}"), FText::FromString(AssetName)));
}

void UAnimGraphNode_KawaiiPhysics::ExportBoneConstraintsDataAsset()
{
	FString AssetName;
	UPackage* Package = CreateDataAssetPackage(TEXT("Choose Location for BoneConstraints Data Asset"),
	                                           TEXT("_BoneConstraint"), AssetName);
	if (!Package)
	{
		return;
	}

	UKawaiiPhysicsBoneConstraintsDataAsset* NewAsset = NewObject<UKawaiiPhysicsBoneConstraintsDataAsset>(
		Package, UKawaiiPhysicsBoneConstraintsDataAsset::StaticClass(), FName(*AssetName),
		RF_Public | RF_Standalone);
	if (!NewAsset)
	{
		return;
	}

	if (USkeleton* Skeleton = GetSkeletonBeingDebugged())
	{
		NewAsset->PreviewSkeleton = Skeleton;
		NewAsset->UpdatePreviewBoneList();
	}

	// The fork keeps constraints per chain. Constraints name their bones, so
	// every chain's go into the one asset.
	for (const FKawaiiPhysicsChain& Chain : Node.Chains)
	{
		for (const FModifyBoneConstraint& Constraint : Chain.BoneConstraintSettings.BoneConstraints)
		{
			NewAsset->BoneConstraintsData.AddDefaulted_GetRef().Update(Constraint);
		}
	}

	USelection* Selection = GEditor->GetSelectedObjects();
	Selection->DeselectAll();
	Selection->Select(NewAsset);

	FAssetRegistryModule::AssetCreated(NewAsset);
	Package->MarkPackageDirty();

	ShowExportAssetNotification(NewAsset, FText::Format(
		LOCTEXT("ExportedBoneConstraintsDataAsset", "Exported BoneConstraints Data Asset: {0}"),
		FText::FromString(AssetName)));
}


void UAnimGraphNode_KawaiiPhysics::CustomizeDetailDebugVisualizations(IDetailLayoutBuilder& DetailBuilder)
{
	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(TEXT("Debug Visualization"));
	Category.SetSortOrder(0);

	struct FToggle
	{
		bool UAnimGraphNode_KawaiiPhysics::* Flag;
		FText Label;
	};
	const FToggle Toggles[] =
	{
		{&UAnimGraphNode_KawaiiPhysics::bEnableDebugDrawBone, LOCTEXT("ShowBone", "Bone")},
		{&UAnimGraphNode_KawaiiPhysics::bEnableDebugBoneLengthRate, LOCTEXT("ShowLengthRate", "Length Rate")},
		{&UAnimGraphNode_KawaiiPhysics::bEnableDebugDrawLimitAngle, LOCTEXT("ShowLimitAngle", "Limit Angle")},
		{&UAnimGraphNode_KawaiiPhysics::bEnableDebugDrawSphereLimit, LOCTEXT("ShowSphere", "Sphere")},
		{&UAnimGraphNode_KawaiiPhysics::bEnableDebugDrawCapsuleLimit, LOCTEXT("ShowCapsule", "Capsule")},
		{&UAnimGraphNode_KawaiiPhysics::bEnableDebugDrawBoxLimit, LOCTEXT("ShowBox", "Box")},
		{&UAnimGraphNode_KawaiiPhysics::bEnableDebugDrawPlanerLimit, LOCTEXT("ShowPlane", "Plane")},
		{&UAnimGraphNode_KawaiiPhysics::bEnableDebugDrawBoneConstraint, LOCTEXT("ShowConstraint", "Constraint")},
		{&UAnimGraphNode_KawaiiPhysics::bEnableDebugDrawExternalForce, LOCTEXT("ShowForce", "Ext. Force")},
	};

	TSharedRef<SUniformGridPanel> Grid = SNew(SUniformGridPanel).SlotPadding(FMargin(2, 2));
	constexpr int32 Columns = 5;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Toggles); ++Index)
	{
		const FToggle Toggle = Toggles[Index];
		Grid->AddSlot(Index % Columns, Index / Columns)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked_Lambda([this, Toggle]()
			{
				this->*Toggle.Flag = !(this->*Toggle.Flag);
				return FReply::Handled();
			})
			.ButtonColorAndOpacity_Lambda([this, Toggle]()
			{
				// Green = shown, red = hidden, as upstream.
				return (this->*Toggle.Flag)
					       ? FAppStyle::Get().GetSlateColor("Colors.AccentGreen")
					       : FAppStyle::Get().GetSlateColor("Colors.AccentRed");
			})
			[
				SNew(STextBlock).Text(Toggle.Label)
			]
		];
	}

	Category.AddCustomRow(LOCTEXT("DebugVisualizationRow", "Debug Visualization"))
	[
		Grid
	];
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
