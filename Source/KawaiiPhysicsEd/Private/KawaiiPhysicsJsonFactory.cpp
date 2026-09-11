// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License

#include "KawaiiPhysicsJsonFactory.h"

#include "KawaiiPhysicsEd.h"
#include "KawaiiPhysicsJsonImporter.h"
#include "Animation/Skeleton.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Factories/AnimBlueprintFactory.h"
#include "Misc/FileHelper.h"
#include "Misc/App.h"
#include "Misc/MessageDialog.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#define LOCTEXT_NAMESPACE "KawaiiPhysicsJsonFactory"

namespace
{
	/** Strips FModel's trailing export index: "/Game/Foo/Bar.55" -> "/Game/Foo/Bar". */
	/** Dialogs must not block a commandlet, cook or automation run. */
	void TellUser(const FText& Message, const FText& Title)
	{
		if (FApp::IsUnattended() || IsRunningCommandlet() || GIsAutomationTesting)
		{
			UE_LOG(LogKawaiiPhysicsEd, Display, TEXT("%s"), *Message.ToString());
			return;
		}

		FMessageDialog::Open(EAppMsgType::Ok, Message, Title);
	}

	FString StripExportIndex(FString Path)
	{
		int32 LastDot = INDEX_NONE;
		if (Path.FindLastChar(TEXT('.'), LastDot) && Path.Mid(LastDot + 1).IsNumeric())
		{
			Path = Path.Left(LastDot);
		}
		return Path;
	}
}

UKawaiiPhysicsJsonFactory::UKawaiiPhysicsJsonFactory()
{
	bCreateNew = false;
	bEditorImport = true;
	bText = false;

	SupportedClass = UAnimBlueprint::StaticClass();
	Formats.Add(TEXT("json;Kawaii Physics Anim Blueprint export"));

	// ".json" is claimed by other tools too (J2A_Plugin, DataTable import).
	// Bid above them: FactoryCanImport below is narrow enough that winning the
	// bid only ever takes files that are genuinely Kawaii Physics exports.
	//
	// The margin is deliberately wide. J2A_Plugin also bids +100, and UE resolves
	// a tie by registration order -- which of the two handles a drop then depends
	// on plugin load order rather than on anything meaningful. It logs
	// "Two factories registered with same priority" when that happens.
	ImportPriority = DefaultImportPriority + 1000;
}

FText UKawaiiPhysicsJsonFactory::GetDisplayName() const
{
	return LOCTEXT("FactoryName", "Kawaii Physics Anim Blueprint (FModel JSON)");
}

bool UKawaiiPhysicsJsonFactory::FactoryCanImport(const FString& Filename)
{
	// ".json" is far too common to claim outright, so only accept files that
	// actually look like an FModel export of an AnimBlueprint containing Kawaii
	// Physics nodes. Anything else is left to whatever else handles JSON.
	if (!Filename.EndsWith(TEXT(".json"), ESearchCase::IgnoreCase))
	{
		return false;
	}

	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Filename))
	{
		return false;
	}

	const bool bCanImport = Text.Contains(TEXT("AnimBlueprintGeneratedClass"))
		&& Text.Contains(TEXT("AnimNode_KawaiiPhysics"));

	UE_LOG(LogKawaiiPhysicsEd, Verbose, TEXT("FactoryCanImport('%s') = %s"), *Filename,
	       bCanImport ? TEXT("true") : TEXT("false"));

	return bCanImport;
}

USkeleton* UKawaiiPhysicsJsonFactory::ResolveSkeleton(const FString& JsonText, FString& OutWantedPath)
{
	TArray<TSharedPtr<FJsonValue>> Exports;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Exports))
	{
		return nullptr;
	}

	// The generated class records the skeleton it was compiled against.
	for (const TSharedPtr<FJsonValue>& ExportValue : Exports)
	{
		if (!ExportValue.IsValid() || ExportValue->Type != EJson::Object)
		{
			continue;
		}

		const TSharedPtr<FJsonObject> Export = ExportValue->AsObject();

		FString Type;
		Export->TryGetStringField(TEXT("Type"), Type);
		if (Type != TEXT("AnimBlueprintGeneratedClass"))
		{
			continue;
		}

		const TSharedPtr<FJsonObject>* Properties = nullptr;
		if (!Export->TryGetObjectField(TEXT("Properties"), Properties) || !Properties)
		{
			continue;
		}

		const TSharedPtr<FJsonObject>* SkeletonRef = nullptr;
		if (!(*Properties)->TryGetObjectField(TEXT("TargetSkeleton"), SkeletonRef) || !SkeletonRef)
		{
			continue;
		}

		FString ObjectPath;
		(*SkeletonRef)->TryGetStringField(TEXT("ObjectPath"), ObjectPath);
		OutWantedPath = StripExportIndex(ObjectPath);

		if (OutWantedPath.IsEmpty())
		{
			return nullptr;
		}

		FString AssetName;
		OutWantedPath.Split(TEXT("/"), nullptr, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);

		// 1. The game's own path, which is where an extraction usually lands.
		if (USkeleton* Exact = LoadObject<USkeleton>(nullptr, *(OutWantedPath + TEXT(".") + AssetName),
		                                             nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			return Exact;
		}

		// 2. Otherwise any skeleton of that name, wherever it was imported to.
		const FAssetRegistryModule& AssetRegistryModule =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

		TArray<FAssetData> Skeletons;
		AssetRegistryModule.Get().GetAssetsByClass(USkeleton::StaticClass()->GetClassPathName(), Skeletons);

		for (const FAssetData& Candidate : Skeletons)
		{
			if (Candidate.AssetName.ToString().Equals(AssetName, ESearchCase::IgnoreCase))
			{
				return Cast<USkeleton>(Candidate.GetAsset());
			}
		}

		return nullptr;
	}

	return nullptr;
}

UObject* UKawaiiPhysicsJsonFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName,
                                                      EObjectFlags Flags, const FString& Filename,
                                                      const TCHAR* Parms, FFeedbackContext* Warn,
                                                      bool& bOutOperationCanceled)
{
	bOutOperationCanceled = false;

	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *Filename))
	{
		UE_LOG(LogKawaiiPhysicsEd, Error, TEXT("Kawaii Physics import: could not read '%s'."), *Filename);
		bOutOperationCanceled = true;
		return nullptr;
	}

	// The skeleton is not needed to convert, and no Anim Blueprint is created
	// from a drop -- it is only handed to stub PoseAssets so they open cleanly
	// in the editor instead of looking broken.
	FString WantedSkeletonPath;
	USkeleton* Skeleton = ResolveSkeleton(JsonText, WantedSkeletonPath);

	// A drop converts and nothing else: the whole graph, Pose Drivers included,
	// lands on the clipboard as T3D for Ctrl+V into whichever AnimGraph the user
	// actually wants it in. Creating an Anim Blueprint here would guess at that
	// choice, and guessing wrong leaves an unwanted asset behind.
	//
	// Referenced assets (pose assets, limits data assets) are still stubbed and
	// saved, because the pasted nodes have to resolve them.
	const FKawaiiPhysicsJsonImportResult Result =
		UKawaiiPhysicsJsonImporter::CopyNodesToClipboardFromJson(JsonText, Skeleton, true);

	UE_LOG(LogKawaiiPhysicsEd, Log, TEXT("Kawaii Physics import from '%s': %s"), *Filename, *Result.Message);

	FText Message = FText::FromString(Result.Message);

	if (!Skeleton && !WantedSkeletonPath.IsEmpty())
	{
		// Only worth mentioning when a stub actually wanted a skeleton, since
		// nothing else about the conversion depends on one.
		Message = FText::Format(
			LOCTEXT("NoSkeleton",
			        "{0}\n\nThis export's skeleton is not in the project, so any placeholder "
			        "pose asset has none set.\n\nExpected:\n{1}"),
			Message, FText::FromString(WantedSkeletonPath));
	}

	TellUser(Message, LOCTEXT("ImportTitle", "Kawaii Physics Import"));

	// Returning null with the flag set tells the import pipeline "handled, no
	// asset produced" -- without it the drop is reported as a failed import.
	bOutOperationCanceled = true;
	return nullptr;
}

#undef LOCTEXT_NAMESPACE
