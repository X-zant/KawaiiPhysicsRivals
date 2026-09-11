// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License

#include "KawaiiPhysicsEd.h"

#include "KawaiiPhysicsEditMode.h"
#include "KawaiiPhysicsJsonImporter.h"

#include "Animation/AnimBlueprint.h"
#include "ContentBrowserMenuContexts.h"
#include "EditorModeRegistry.h"
#include "Misc/MessageDialog.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "FKawaiiPhysicsEdModule"

DEFINE_LOG_CATEGORY(LogKawaiiPhysicsEd);

namespace
{
	const FName KawaiiPhysicsEditModeId = "AnimGraph.SkeletalControl.KawaiiPhysics";

	/**
	 * Adds "Import Kawaii Physics from JSON..." to the Anim Blueprint asset
	 * context menu, which is where a modder already is when they have an FModel
	 * export in hand.
	 */
	void RegisterAssetContextMenu()
	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AssetContextMenu.AnimBlueprint");
		if (!Menu)
		{
			return;
		}

		FToolMenuSection& Section = Menu->FindOrAddSection("GetAssetActions");

		Section.AddDynamicEntry("KawaiiPhysicsImportJson", FNewToolMenuSectionDelegate::CreateLambda(
			                        [](FToolMenuSection& InSection)
			                        {
				                        const UContentBrowserAssetContextMenuContext* Context =
					                        InSection.FindContext<UContentBrowserAssetContextMenuContext>();
				                        if (!Context)
				                        {
					                        return;
				                        }

				                        TArray<UAnimBlueprint*> Selected =
					                        Context->LoadSelectedObjects<UAnimBlueprint>();
				                        if (Selected.Num() != 1)
				                        {
					                        // Importing into several Blueprints at once has no sensible
					                        // meaning -- each needs its own export.
					                        return;
				                        }

				                        UAnimBlueprint* TargetAnimBlueprint = Selected[0];

				                        InSection.AddMenuEntry(
					                        "KawaiiPhysicsImportJsonEntry",
					                        LOCTEXT("ImportJsonLabel", "Import Kawaii Physics from JSON..."),
					                        LOCTEXT("ImportJsonTooltip",
					                                "Rebuild Kawaii Physics nodes from an FModel JSON export of a "
					                                "cooked Physics Anim Blueprint, and wire them in their original "
					                                "order."),
					                        FSlateIcon(),
					                        FUIAction(FExecuteAction::CreateLambda([TargetAnimBlueprint]()
					                        {
						                        const FKawaiiPhysicsJsonImportResult Result =
							                        UKawaiiPhysicsJsonImporter::ImportWithFilePicker(
								                        TargetAnimBlueprint);

						                        if (Result.Message.Equals(TEXT("Cancelled.")))
						                        {
							                        return;
						                        }

						                        FMessageDialog::Open(
							                        Result.bSuccess ? EAppMsgType::Ok : EAppMsgType::Ok,
							                        FText::FromString(Result.Message),
							                        Result.bSuccess
								                        ? LOCTEXT("ImportDoneTitle", "Kawaii Physics Import")
								                        : LOCTEXT("ImportFailedTitle",
								                                  "Kawaii Physics Import Failed"));
					                        })));
			                        }));
	}

	void RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(FName("KawaiiPhysicsEd"));
		RegisterAssetContextMenu();
	}
}

void FKawaiiPhysicsEdModule::StartupModule()
{
	FEditorModeRegistry::Get().RegisterMode<FKawaiiPhysicsEditMode>(
		KawaiiPhysicsEditModeId,
		LOCTEXT("KawaiiPhysicsEditMode", "Kawaii Physics"),
		FSlateIcon(),
		false);

	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateStatic(&RegisterMenus));
}

void FKawaiiPhysicsEdModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(FName("KawaiiPhysicsEd"));

	FEditorModeRegistry::Get().UnregisterMode(KawaiiPhysicsEditModeId);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FKawaiiPhysicsEdModule, KawaiiPhysicsEd)
