// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License

#include "KawaiiPhysicsJsonImporter.h"

#include "AnimGraphNode_KawaiiPhysics.h"
#include "AnimGraphNode_KawaiiPhysicsMerge.h"
#include "AnimNode_KawaiiPhysics.h"
#include "AnimNode_KawaiiPhysicsMerge.h"
#include "KawaiiPhysicsExternalForce.h"
#include "KawaiiPhysicsEd.h"

#include "AnimGraphNode_PoseDriver.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/PoseAsset.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AnimNodes/AnimNode_PoseDriver.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "DesktopPlatformModule.h"
#include "AnimationGraph.h"
#include "AnimationGraphSchema.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphUtilities.h"
#include "HAL/PlatformApplicationMisc.h"
#include "EdGraph/EdGraphPin.h"
#include "Framework/Application/SlateApplication.h"
#include "IDesktopPlatform.h"
#include "JsonObjectConverter.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#define LOCTEXT_NAMESPACE "KawaiiPhysicsJsonImporter"

namespace
{
	/** Property name prefix the anim BP compiler gives every generated node. */
	const TCHAR* AnimNodePropertyPrefix = TEXT("AnimGraphNode_");

	/**
	 * Pulls "AnimNode_KawaiiPhysics" out of an FModel struct reference such as
	 * { "ObjectName": "Class'AnimNode_KawaiiPhysics'", "ObjectPath": "/Script/KawaiiPhysics" }.
	 *
	 * The class export declares the exact struct behind every node property, so
	 * node types are read rather than guessed at from their field names.
	 */
	FString ReadStructTypeName(const TSharedPtr<FJsonObject>& PropertyEntry)
	{
		const TSharedPtr<FJsonObject>* StructObject = nullptr;
		if (!PropertyEntry->TryGetObjectField(TEXT("Struct"), StructObject) || !StructObject)
		{
			return FString();
		}

		FString ObjectName;
		if (!(*StructObject)->TryGetStringField(TEXT("ObjectName"), ObjectName))
		{
			return FString();
		}

		int32 Open = INDEX_NONE;
		int32 Close = INDEX_NONE;
		const TCHAR Quote = TEXT('\'');  // a single quote
		if (ObjectName.FindChar(Quote, Open) && ObjectName.FindLastChar(Quote, Close) && Close > Open)
		{
			return ObjectName.Mid(Open + 1, Close - Open - 1);
		}

		return ObjectName;
	}


	/**
	 * Works out which external-force preset a JSON object represents.
	 *
	 * FModel flattens FInstancedStruct to the concrete struct's fields and emits
	 * no type tag, so the type has to be recovered from a field that only that
	 * type has. Ordered most distinctive first.
	 */
	UScriptStruct* GuessExternalForceType(const TSharedPtr<FJsonObject>& Obj)
	{
		if (!Obj.IsValid())
		{
			return nullptr;
		}

		if (Obj->HasField(TEXT("ForceCurve")) || Obj->HasField(TEXT("CurveEvaluateType")))
		{
			return FKawaiiPhysics_ExternalForce_Curve::StaticStruct();
		}

		if (Obj->HasField(TEXT("bUseCharacterGravityDirection")) ||
			Obj->HasField(TEXT("bUseCharacterGravityScale")) ||
			Obj->HasField(TEXT("OverrideGravityDirection")) ||
			Obj->HasField(TEXT("bUseOverrideGravityDirection")))
		{
			return FKawaiiPhysics_ExternalForce_Gravity::StaticStruct();
		}

		if (Obj->HasField(TEXT("ForceDir")) || Obj->HasField(TEXT("Interval")))
		{
			return FKawaiiPhysics_ExternalForce_Basic::StaticStruct();
		}

		// Wind is the only remaining type, and its one distinctive field is a
		// transient world pointer that may legitimately be absent.
		if (Obj->HasField(TEXT("World")) || Obj->HasField(TEXT("ForceRateByBoneLengthRate")))
		{
			return FKawaiiPhysics_ExternalForce_Wind::StaticStruct();
		}

		return nullptr;
	}

	/** Rebuilds a TArray<FInstancedStruct> of force presets from its JSON array. */
	void ImportExternalForces(const TSharedPtr<FJsonObject>& OwnerJson, TArray<FInstancedStruct>& OutForces,
	                          TArray<FString>& OutUnrecognised)
	{
		OutForces.Reset();

		if (!OwnerJson.IsValid())
		{
			return;
		}

		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (!OwnerJson->TryGetArrayField(TEXT("ExternalForces"), Array) || !Array)
		{
			return;
		}

		for (const TSharedPtr<FJsonValue>& Element : *Array)
		{
			if (!Element.IsValid() || Element->Type != EJson::Object)
			{
				continue;
			}

			const TSharedPtr<FJsonObject> ForceObject = Element->AsObject();
			UScriptStruct* ForceType = GuessExternalForceType(ForceObject);
			if (!ForceType)
			{
				OutUnrecognised.AddUnique(TEXT("<unrecognised external force preset>"));
				continue;
			}

			FInstancedStruct NewForce;
			NewForce.InitializeAs(ForceType);

			if (FJsonObjectConverter::JsonObjectToUStruct(ForceObject.ToSharedRef(), ForceType,
			                                              NewForce.GetMutableMemory()))
			{
				OutForces.Add(MoveTemp(NewForce));
			}
			else
			{
				OutUnrecognised.AddUnique(ForceType->GetName());
			}
		}
	}

	/** Reads ComponentPose.LinkID, which indexes the class's anim-node property list. */
	int32 ReadComponentPoseLinkId(const TSharedPtr<FJsonObject>& NodeObject)
	{
		const TSharedPtr<FJsonObject>* PoseObject = nullptr;
		if (NodeObject->TryGetObjectField(TEXT("ComponentPose"), PoseObject) && PoseObject)
		{
			int32 LinkId = INDEX_NONE;
			if ((*PoseObject)->TryGetNumberField(TEXT("LinkID"), LinkId))
			{
				return LinkId;
			}
		}

		return INDEX_NONE;
	}

	/** "EBoneForwardAxis::X_Positive" -> "X_Positive", which is what the converter wants. */
	bool TryStripEnumQualifier(const FString& In, FString& Out)
	{
		int32 SeparatorIndex = INDEX_NONE;
		if (!In.FindChar(TEXT(':'), SeparatorIndex) || SeparatorIndex == 0)
		{
			return false;
		}

		if (!In.Mid(SeparatorIndex, 2).Equals(TEXT("::")))
		{
			return false;
		}

		// Only rewrite things that actually look like an enum literal, so bone
		// names or paths containing a colon are left alone.
		const FString TypeName = In.Left(SeparatorIndex);
		if (TypeName.Len() < 2 || TypeName[0] != TEXT('E'))
		{
			return false;
		}

		Out = In.Mid(SeparatorIndex + 2);
		return !Out.IsEmpty();
	}

	/** An FModel object reference is an object carrying ObjectName/ObjectPath. */
	bool IsObjectReference(const TSharedPtr<FJsonObject>& Object)
	{
		return Object.IsValid() && Object->HasField(TEXT("ObjectPath")) && Object->HasField(TEXT("ObjectName"));
	}

	/**
	 * FModel writes references as ClassName'AssetName' plus a package path with a
	 * trailing export index, e.g.
	 *   ObjectName: "PoseAsset'SK_1066_1066001_Skeleton_PoseAsset'"
	 *   ObjectPath: "/Game/.../SK_1066_1066001_Skeleton_PoseAsset.0"
	 *
	 * The path is already a UE package path once the ".0" is removed, and the
	 * class is the part before the quote.
	 */
	void SplitObjectReference(const TSharedPtr<FJsonObject>& Ref, FString& OutClassName, FString& OutAssetName,
	                          FString& OutPackagePath)
	{
		FString ObjectName;
		Ref->TryGetStringField(TEXT("ObjectName"), ObjectName);

		const TCHAR Quote = TEXT('\'');  // a single quote
		int32 Open = INDEX_NONE;
		int32 Close = INDEX_NONE;
		if (ObjectName.FindChar(Quote, Open) && ObjectName.FindLastChar(Quote, Close) && Close > Open)
		{
			OutClassName = ObjectName.Left(Open);
			OutAssetName = ObjectName.Mid(Open + 1, Close - Open - 1);
		}
		else
		{
			OutAssetName = ObjectName;
		}

		FString ObjectPath;
		Ref->TryGetStringField(TEXT("ObjectPath"), ObjectPath);

		// Drop the trailing export index that FModel appends.
		int32 LastDot = INDEX_NONE;
		if (ObjectPath.FindLastChar(TEXT('.'), LastDot))
		{
			const FString Tail = ObjectPath.Mid(LastDot + 1);
			if (Tail.IsNumeric())
			{
				ObjectPath = ObjectPath.Left(LastDot);
			}
		}

		OutPackagePath = ObjectPath;
	}

	/**
	 * Returns a live object at the game's own package path, creating an empty
	 * placeholder if nothing is there.
	 *
	 * The placeholder's contents do not matter: it exists so the reference
	 * serialises with the correct path and class, and the packaged game resolves
	 * it against its own data.
	 */
	UObject* FindOrCreateStubAsset(const FString& PackagePath, const FString& AssetName, const FString& ClassName,
	                               USkeleton* FallbackSkeleton, TArray<FString>& OutCreatedStubs)
	{
		if (PackagePath.IsEmpty() || AssetName.IsEmpty())
		{
			return nullptr;
		}

		const FString FullPath = PackagePath + TEXT(".") + AssetName;

		if (UObject* Existing = StaticLoadObject(UObject::StaticClass(), nullptr, *FullPath, nullptr,
		                                         LOAD_NoWarn | LOAD_Quiet))
		{
			return Existing;
		}

		UClass* AssetClass = UClass::TryFindTypeSlow<UClass>(ClassName);
		if (!AssetClass)
		{
			return nullptr;
		}

		UPackage* Package = CreatePackage(*PackagePath);
		if (!Package)
		{
			return nullptr;
		}

		UObject* Stub = NewObject<UObject>(Package, AssetClass, FName(*AssetName),
		                                   RF_Public | RF_Standalone | RF_Transactional);
		if (!Stub)
		{
			return nullptr;
		}

		// A PoseAsset without a skeleton cannot be opened in the editor, which
		// makes the stub look broken rather than empty.
		if (UPoseAsset* PoseAsset = Cast<UPoseAsset>(Stub))
		{
			if (FallbackSkeleton)
			{
				PoseAsset->SetSkeleton(FallbackSkeleton);
			}
		}

		FAssetRegistryModule::AssetCreated(Stub);
		Package->MarkPackageDirty();

		// Write it out now rather than leaving it dirty in memory. A drop creates
		// no asset of its own, so nothing else would ever prompt to save these --
		// they would silently vanish when the editor closed, and the pasted nodes
		// would come back with empty references.
		const FString FileName = FPackageName::LongPackageNameToFilename(
			PackagePath, FPackageName::GetAssetPackageExtension());

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;

		if (!UPackage::SavePackage(Package, Stub, *FileName, SaveArgs))
		{
			// Not fatal: the object still exists in memory, so the paste resolves
			// for this session. Say so rather than implying it was persisted.
			UE_LOG(LogKawaiiPhysicsEd, Warning,
			       TEXT("Kawaii Physics: created placeholder '%s' but could not save it to '%s'. "
				       "It will be lost when the editor closes."),
			       *FullPath, *FileName);
		}

		OutCreatedStubs.AddUnique(FullPath);
		return Stub;
	}

	/**
	 * Returns the value to use in place of Value, or null to drop the field.
	 *
	 * FJsonValue is immutable, so rewrites have to produce a replacement rather
	 * than edit in place. Objects are the exception: their field map is mutable,
	 * so nested objects are fixed up directly and returned unchanged.
	 */
	TSharedPtr<FJsonValue> SanitiseValue(const TSharedPtr<FJsonValue>& Value, FKawaiiPhysicsImportContext& Context);

	void SanitiseObject(const TSharedPtr<FJsonObject>& Object, FKawaiiPhysicsImportContext& Context)
	{
		if (!Object.IsValid())
		{
			return;
		}

		TArray<FString> FieldsToDrop;
		TMap<FString, TSharedPtr<FJsonValue>> Replacements;

		for (const auto& Pair : Object->Values)
		{
			if (!Pair.Value.IsValid())
			{
				continue;
			}

			// Runtime state, rebuilt from the skeleton on first evaluation. It
			// appears per chain and, on the merge node, per entry in Nodes.
			if (Pair.Key == TEXT("ModifyBones"))
			{
				FieldsToDrop.Add(Pair.Key);
				continue;
			}

			const TSharedPtr<FJsonValue> NewValue = SanitiseValue(Pair.Value, Context);
			if (!NewValue.IsValid())
			{
				FieldsToDrop.Add(Pair.Key);
			}
			else if (NewValue != Pair.Value)
			{
				Replacements.Add(Pair.Key, NewValue);
			}
		}

		for (const FString& Field : FieldsToDrop)
		{
			Object->RemoveField(Field);
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Replacement : Replacements)
		{
			Object->SetField(Replacement.Key, Replacement.Value);
		}
	}

	TSharedPtr<FJsonValue> SanitiseValue(const TSharedPtr<FJsonValue>& Value, FKawaiiPhysicsImportContext& Context)
	{
		switch (Value->Type)
		{
		case EJson::Object:
		{
			const TSharedPtr<FJsonObject> Child = Value->AsObject();

			if (IsObjectReference(Child))
			{
				// Keep the game's own package path. A cooked mod that references
				// it resolves against the game's data at runtime, so the asset
				// here only has to exist with the right path, name and class.
				FString ClassName, AssetName, PackagePath;
				SplitObjectReference(Child, ClassName, AssetName, PackagePath);

				if (Context.bCreateStubAssets)
				{
					if (FindOrCreateStubAsset(PackagePath, AssetName, ClassName, Context.Skeleton,
					                          Context.CreatedStubAssets))
					{
						// A plain string is imported via ImportText_Direct, which
						// resolves the path to the object we just guaranteed.
						return MakeShared<FJsonValueString>(PackagePath + TEXT(".") + AssetName);
					}
				}

				FString ObjectName;
				Child->TryGetStringField(TEXT("ObjectName"), ObjectName);
				Context.UnresolvedReferences.AddUnique(ObjectName);
				return nullptr;
			}

			SanitiseObject(Child, Context);
			return Value;
		}

		case EJson::Array:
		{
			TArray<TSharedPtr<FJsonValue>> NewElements;
			for (const TSharedPtr<FJsonValue>& Element : Value->AsArray())
			{
				if (!Element.IsValid())
				{
					continue;
				}

				if (const TSharedPtr<FJsonValue> NewElement = SanitiseValue(Element, Context))
				{
					NewElements.Add(NewElement);
				}
			}

			return MakeShared<FJsonValueArray>(NewElements);
		}

		case EJson::String:
		{
			FString Stripped;
			if (TryStripEnumQualifier(Value->AsString(), Stripped))
			{
				return MakeShared<FJsonValueString>(Stripped);
			}
			return Value;
		}

		default:
			return Value;
		}
	}
}

void UKawaiiPhysicsJsonImporter::SanitiseNodeJson(const TSharedPtr<FJsonObject>& NodeObject,
                                                  FKawaiiPhysicsImportContext& Context)
{
	if (!NodeObject.IsValid())
	{
		return;
	}

	// Pose links are graph topology, not data; they are restored separately.
	NodeObject->RemoveField(TEXT("ComponentPose"));
	NodeObject->RemoveField(TEXT("LocalPose"));

	SanitiseObject(NodeObject, Context);
}

bool UKawaiiPhysicsJsonImporter::ParseNodeStructWithContext(const TSharedPtr<FJsonObject>& NodeObject,
                                                            const UScriptStruct* NodeStruct, void* OutNode,
                                                            FKawaiiPhysicsImportContext& Context)
{
	if (!NodeObject.IsValid() || !NodeStruct || !OutNode)
	{
		return false;
	}

	SanitiseNodeJson(NodeObject, Context);

	if (!FJsonObjectConverter::JsonObjectToUStruct(NodeObject.ToSharedRef(), NodeStruct, OutNode))
	{
		return false;
	}

	// External force presets are FInstancedStruct, which has no reflected
	// properties and carries no type tag in the export -- the generic converter
	// produces empty entries for them. Rebuild them explicitly.
	TArray<FString> Unrecognised;

	if (NodeStruct == FAnimNode_KawaiiPhysics::StaticStruct())
	{
		FAnimNode_KawaiiPhysics* KawaiiNode = static_cast<FAnimNode_KawaiiPhysics*>(OutNode);

		const TArray<TSharedPtr<FJsonValue>>* ChainsJson = nullptr;
		if (NodeObject->TryGetArrayField(TEXT("Chains"), ChainsJson) && ChainsJson)
		{
			for (int32 ChainIndex = 0; ChainIndex < ChainsJson->Num(); ++ChainIndex)
			{
				if (!KawaiiNode->Chains.IsValidIndex(ChainIndex))
				{
					break;
				}

				const TSharedPtr<FJsonValue>& ChainValue = (*ChainsJson)[ChainIndex];
				if (!ChainValue.IsValid() || ChainValue->Type != EJson::Object)
				{
					continue;
				}

				const TSharedPtr<FJsonObject>* ForceSettings = nullptr;
				if (ChainValue->AsObject()->TryGetObjectField(TEXT("ExternalForceSettings"), ForceSettings) &&
					ForceSettings)
				{
					ImportExternalForces(*ForceSettings,
					                     KawaiiNode->Chains[ChainIndex].ExternalForceSettings.ExternalForces,
					                     Unrecognised);
					Context.ExternalForcesRebuilt +=
						KawaiiNode->Chains[ChainIndex].ExternalForceSettings.ExternalForces.Num();
				}
			}
		}
	}
	else if (NodeStruct == FAnimNode_KawaiiPhysicsMerge::StaticStruct())
	{
		// The merge node holds its force presets on the node itself.
		FAnimNode_KawaiiPhysicsMerge* MergeNode = static_cast<FAnimNode_KawaiiPhysicsMerge*>(OutNode);
		ImportExternalForces(NodeObject, MergeNode->ExternalForces, Unrecognised);
		Context.ExternalForcesRebuilt += MergeNode->ExternalForces.Num();
	}

	for (const FString& Name : Unrecognised)
	{
		UE_LOG(LogKawaiiPhysicsEd, Warning,
		       TEXT("Kawaii Physics import: could not rebuild external force preset '%s'."), *Name);
	}

	return true;
}

bool UKawaiiPhysicsJsonImporter::ParseNodeStruct(const TSharedPtr<FJsonObject>& NodeObject,
                                                 const UScriptStruct* NodeStruct, void* OutNode,
                                                 TArray<FString>& OutUnresolvedReferences)
{
	// No stubbing: used by tests and by callers that only want the data.
	FKawaiiPhysicsImportContext Context;
	const bool bResult = ParseNodeStructWithContext(NodeObject, NodeStruct, OutNode, Context);
	OutUnresolvedReferences.Append(Context.UnresolvedReferences);
	return bResult;
}

bool UKawaiiPhysicsJsonImporter::ParseKawaiiNode(const TSharedPtr<FJsonObject>& NodeObject,
                                                 FAnimNode_KawaiiPhysics& OutNode,
                                                 TArray<FString>& OutUnresolvedReferences)
{
	return ParseNodeStruct(NodeObject, FAnimNode_KawaiiPhysics::StaticStruct(), &OutNode,
	                       OutUnresolvedReferences);
}


namespace
{
	/** Fields the importer removes on purpose, with the reason. */
	const TMap<FString, FString>& IntentionalDrops()
	{
		static const TMap<FString, FString> Map = {
			{TEXT("ComponentPose"), TEXT("pose link - restored as a graph connection instead")},
			{TEXT("LocalPose"), TEXT("pose link - restored as a graph connection instead")},
			{TEXT("ModifyBones"), TEXT("runtime state - rebuilt from the skeleton on first evaluation")},
		};
		return Map;
	}

	void AuditObject(const TSharedPtr<FJsonObject>& Obj, const UStruct* Struct, const FString& Path,
	                 TArray<FString>& OutReport);

	void AuditValue(const TSharedPtr<FJsonValue>& Value, const FProperty* Property, const FString& Path,
	                TArray<FString>& OutReport)
	{
		if (!Value.IsValid() || !Property)
		{
			return;
		}

		if (const FStructProperty* StructProp = CastField<FStructProperty>(Property))
		{
			if (Value->Type == EJson::Object)
			{
				AuditObject(Value->AsObject(), StructProp->Struct, Path, OutReport);
			}
			return;
		}

		if (const FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
		{
			const FStructProperty* Inner = CastField<FStructProperty>(ArrayProp->Inner);
			if (Inner && Value->Type == EJson::Array)
			{
				const TArray<TSharedPtr<FJsonValue>>& Elements = Value->AsArray();
				for (int32 i = 0; i < Elements.Num(); ++i)
				{
					if (Elements[i].IsValid() && Elements[i]->Type == EJson::Object)
					{
						AuditObject(Elements[i]->AsObject(), Inner->Struct,
						            FString::Printf(TEXT("%s[%d]"), *Path, i), OutReport);
					}
				}
			}
		}
	}

	void AuditObject(const TSharedPtr<FJsonObject>& Obj, const UStruct* Struct, const FString& Path,
	                 TArray<FString>& OutReport)
	{
		if (!Obj.IsValid() || !Struct)
		{
			return;
		}

		for (const auto& Pair : Obj->Values)
		{
			const FString FieldPath = Path.IsEmpty() ? Pair.Key : Path + TEXT(".") + Pair.Key;

			if (const FString* Reason = IntentionalDrops().Find(Pair.Key))
			{
				OutReport.Add(FString::Printf(TEXT("[intentional] %s  (%s)"), *FieldPath, **Reason));
				continue;
			}

			const FProperty* Property = Struct->FindPropertyByName(FName(*Pair.Key));
			if (!Property)
			{
				OutReport.Add(FString::Printf(TEXT("[DROPPED]     %s"), *FieldPath));
				continue;
			}

			// FInstancedStruct has no reflected properties; its contents are
			// rebuilt separately, so do not descend into it.
			if (const FStructProperty* StructProp = CastField<FStructProperty>(Property))
			{
				if (StructProp->Struct == FInstancedStruct::StaticStruct())
				{
					continue;
				}
			}
			if (const FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
			{
				if (const FStructProperty* Inner = CastField<FStructProperty>(ArrayProp->Inner))
				{
					if (Inner->Struct == FInstancedStruct::StaticStruct())
					{
						continue;
					}
				}
			}

			AuditValue(Pair.Value, Property, FieldPath, OutReport);
		}
	}
}

TArray<FString> UKawaiiPhysicsJsonImporter::AuditJsonCoverage(const FString& JsonFilePath)
{
	TArray<FString> Report;

	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *JsonFilePath))
	{
		Report.Add(TEXT("could not read file"));
		return Report;
	}

	TArray<TSharedPtr<FJsonValue>> Exports;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Exports))
	{
		Report.Add(TEXT("not a valid FModel package export"));
		return Report;
	}

	for (const TSharedPtr<FJsonValue>& ExportValue : Exports)
	{
		if (!ExportValue.IsValid() || ExportValue->Type != EJson::Object) continue;
		const TSharedPtr<FJsonObject> Export = ExportValue->AsObject();

		FString Name;
		Export->TryGetStringField(TEXT("Name"), Name);
		if (!Name.StartsWith(TEXT("Default__"))) continue;

		const TSharedPtr<FJsonObject>* Properties = nullptr;
		if (!Export->TryGetObjectField(TEXT("Properties"), Properties) || !Properties) continue;

		for (const auto& Pair : (*Properties)->Values)
		{
			if (!Pair.Value.IsValid() || Pair.Value->Type != EJson::Object) continue;
			const TSharedPtr<FJsonObject> NodeJson = Pair.Value->AsObject();

			const UScriptStruct* NodeStruct = nullptr;
			if (NodeJson->HasField(TEXT("Chains")))
			{
				NodeStruct = FAnimNode_KawaiiPhysics::StaticStruct();
			}
			else if (NodeJson->HasField(TEXT("Nodes")) && NodeJson->HasField(TEXT("MergedBoneConstraints")))
			{
				NodeStruct = FAnimNode_KawaiiPhysicsMerge::StaticStruct();
			}

			if (!NodeStruct) continue;

			AuditObject(NodeJson, NodeStruct, Pair.Key, Report);
		}
	}

	return Report;
}

UEdGraph* UKawaiiPhysicsJsonImporter::FindAnimGraph(const UAnimBlueprint* AnimBlueprint)
{
	if (!AnimBlueprint)
	{
		return nullptr;
	}

	TArray<UEdGraph*> Graphs;
	AnimBlueprint->GetAllGraphs(Graphs);

	for (UEdGraph* Graph : Graphs)
	{
		if (Graph && Graph->GetFName() == TEXT("AnimGraph"))
		{
			return Graph;
		}
	}

	return nullptr;
}

FKawaiiPhysicsJsonImportResult UKawaiiPhysicsJsonImporter::ImportFromJsonFile(UAnimBlueprint* TargetAnimBlueprint,
                                                                              const FString& JsonFilePath,
                                                                              bool bRestoreNodeLinks,
                                                                              bool bCreateStubAssets)
{
	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *JsonFilePath))
	{
		FKawaiiPhysicsJsonImportResult Result;
		Result.Message = FString::Printf(TEXT("Could not read '%s'."), *JsonFilePath);
		return Result;
	}

	return ImportFromJsonString(TargetAnimBlueprint, JsonText, bRestoreNodeLinks, bCreateStubAssets);
}

FKawaiiPhysicsJsonImportResult UKawaiiPhysicsJsonImporter::ImportFromJsonString(UAnimBlueprint* TargetAnimBlueprint,
	const FString& JsonText, bool bRestoreNodeLinks, bool bCreateStubAssets)
{
	UEdGraph* AnimGraph = FindAnimGraph(TargetAnimBlueprint);
	if (!AnimGraph)
	{
		FKawaiiPhysicsJsonImportResult Failed;
		Failed.Message = TEXT("Target has no AnimGraph.");
		return Failed;
	}

	FKawaiiPhysicsJsonImportResult Result = ImportIntoGraph(
		AnimGraph, TargetAnimBlueprint->TargetSkeleton, JsonText, bRestoreNodeLinks, bCreateStubAssets);

	if (Result.bSuccess)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(TargetAnimBlueprint);
	}

	return Result;
}

FKawaiiPhysicsJsonImportResult UKawaiiPhysicsJsonImporter::ImportIntoGraph(UEdGraph* AnimGraph,
	USkeleton* SkeletonForStubs, const FString& JsonText, bool bRestoreNodeLinks, bool bCreateStubAssets)
{
	FKawaiiPhysicsJsonImportResult Result;

	if (!AnimGraph)
	{
		Result.Message = TEXT("No target graph.");
		return Result;
	}

	// FModel exports a package as an array of its exports.
	TArray<TSharedPtr<FJsonValue>> Exports;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Exports))
	{
		Result.Message = TEXT("Not valid JSON, or not an FModel package export (expected a top-level array).");
		return Result;
	}

	// The generated class declares the nodes; the CDO holds their values.
	TSharedPtr<FJsonObject> ClassExport;
	TSharedPtr<FJsonObject> DefaultsExport;

	for (const TSharedPtr<FJsonValue>& ExportValue : Exports)
	{
		if (!ExportValue.IsValid() || ExportValue->Type != EJson::Object)
		{
			continue;
		}

		const TSharedPtr<FJsonObject> Export = ExportValue->AsObject();

		FString Type;
		Export->TryGetStringField(TEXT("Type"), Type);
		if (Type == TEXT("AnimBlueprintGeneratedClass"))
		{
			ClassExport = Export;
			continue;
		}

		FString Name;
		Export->TryGetStringField(TEXT("Name"), Name);
		if (Name.StartsWith(TEXT("Default__")))
		{
			DefaultsExport = Export;
		}
	}

	if (!ClassExport.IsValid() || !DefaultsExport.IsValid())
	{
		Result.Message = TEXT("Export does not contain an AnimBlueprintGeneratedClass and its defaults. "
			"Export the whole cooked AnimBlueprint package from FModel, not a single object.");
		return Result;
	}

	// LinkID indexes the class's anim-node properties in declaration order,
	// counting only the compiler-generated node properties.
	TArray<FString> NodePropertyOrder;
	TArray<FString> NodeStructTypes;
	const TArray<TSharedPtr<FJsonValue>>* ChildProperties = nullptr;
	if (ClassExport->TryGetArrayField(TEXT("ChildProperties"), ChildProperties) && ChildProperties)
	{
		for (const TSharedPtr<FJsonValue>& ChildValue : *ChildProperties)
		{
			if (!ChildValue.IsValid() || ChildValue->Type != EJson::Object)
			{
				continue;
			}

			FString ChildName;
			ChildValue->AsObject()->TryGetStringField(TEXT("Name"), ChildName);
			if (ChildName.StartsWith(AnimNodePropertyPrefix))
			{
				NodePropertyOrder.Add(ChildName);
				NodeStructTypes.Add(ReadStructTypeName(ChildValue->AsObject()));
			}
		}
	}

	const TSharedPtr<FJsonObject>* Properties = nullptr;
	if (!DefaultsExport->TryGetObjectField(TEXT("Properties"), Properties) || !Properties)
	{
		Result.Message = TEXT("Defaults export has no Properties block.");
		return Result;
	}

	// Import in declaration order so node placement is stable run to run.
	FKawaiiPhysicsImportContext Context;
	Context.bCreateStubAssets = bCreateStubAssets;
	// Any valid skeleton constructs a stub; contents are irrelevant to it.
	Context.Skeleton = SkeletonForStubs;

	TMap<int32, UAnimGraphNode_Base*> NodesByLinkIndex;
	TMap<int32, int32> SourceLinkByLinkIndex;

	int32 Column = 0;

	for (int32 LinkIndex = 0; LinkIndex < NodePropertyOrder.Num(); ++LinkIndex)
	{
		const FString& PropertyName = NodePropertyOrder[LinkIndex];

		const TSharedPtr<FJsonObject>* NodeObjectPtr = nullptr;
		if (!(*Properties)->TryGetObjectField(PropertyName, NodeObjectPtr) || !NodeObjectPtr)
		{
			continue;
		}

		const TSharedPtr<FJsonObject> NodeObject = *NodeObjectPtr;
		const FString& StructType = NodeStructTypes[LinkIndex];

		// Only this plugin's own nodes can be recreated. Everything else in a
		// physics AnimBlueprint -- Pose Driver, Constraint, the pose-space
		// conversions -- belongs to the engine and is reported, not invented.
		const bool bIsKawaii = StructType == TEXT("AnimNode_KawaiiPhysics");
		const bool bIsKawaiiMerge = StructType == TEXT("AnimNode_KawaiiPhysicsMerge");
		// Pose Driver is a stock engine node, but a physics AnimBlueprint's
		// drivers are part of the setup being recovered, so they come across too.
		const bool bIsPoseDriver = StructType == TEXT("AnimNode_PoseDriver");

		if (!bIsKawaii && !bIsKawaiiMerge && !bIsPoseDriver)
		{
			if (!StructType.IsEmpty())
			{
				Result.SkippedNodeTypes.AddUnique(StructType);
			}
			continue;
		}

		// Captured before sanitising strips the link.
		SourceLinkByLinkIndex.Add(LinkIndex, ReadComponentPoseLinkId(NodeObject));

		UAnimGraphNode_Base* GraphNode = nullptr;

		// One shape per node type: convert into the runtime struct, then spawn the
		// matching graph node and hand it the struct.
		#define KAWAIIPHYSICS_IMPORT_NODE(RuntimeType, GraphType) 			{ 				RuntimeType ImportedNode; 				if (!ParseNodeStructWithContext(NodeObject, RuntimeType::StaticStruct(), &ImportedNode, Context)) 				{ 					UE_LOG(LogKawaiiPhysicsEd, Warning, 					       TEXT("KawaiiPhysics import: could not convert '%s'; skipped."), *PropertyName); 					continue; 				} 				FGraphNodeCreator<GraphType> NodeCreator(*AnimGraph); 				GraphType* NewNode = NodeCreator.CreateNode(); 				NewNode->Node = ImportedNode; 				NewNode->NodePosX = Column * 400; 				NewNode->NodePosY = 0; 				NodeCreator.Finalize(); 				GraphNode = NewNode; 			}

		if (bIsKawaii)
		{
			KAWAIIPHYSICS_IMPORT_NODE(FAnimNode_KawaiiPhysics, UAnimGraphNode_KawaiiPhysics)
		}
		else if (bIsKawaiiMerge)
		{
			KAWAIIPHYSICS_IMPORT_NODE(FAnimNode_KawaiiPhysicsMerge, UAnimGraphNode_KawaiiPhysicsMerge)
		}
		else
		{
			KAWAIIPHYSICS_IMPORT_NODE(FAnimNode_PoseDriver, UAnimGraphNode_PoseDriver)
		}

		#undef KAWAIIPHYSICS_IMPORT_NODE

		NodesByLinkIndex.Add(LinkIndex, GraphNode);
		++Column;
		++Result.NodesCreated;
	}

	if (Result.NodesCreated == 0)
	{
		Result.Message = TEXT("No Kawaii Physics nodes found in that export.");
		return Result;
	}

	if (bRestoreNodeLinks)
	{
		// Rebuild the original evaluation order. Links to nodes that were not
		// imported (pose conversions, linked input) are left open deliberately:
		// how this chain plugs into the host graph is the modder's decision.
		for (const TPair<int32, UAnimGraphNode_Base*>& Pair : NodesByLinkIndex)
		{
			const int32 SourceLink = SourceLinkByLinkIndex.FindRef(Pair.Key);
			UAnimGraphNode_Base* const* SourceNode = NodesByLinkIndex.Find(SourceLink);
			if (!SourceNode || !*SourceNode)
			{
				continue;
			}

			UEdGraphPin* InputPin = Pair.Value->FindPin(TEXT("ComponentPose"), EGPD_Input);

			UEdGraphPin* OutputPin = nullptr;
			for (UEdGraphPin* Pin : (*SourceNode)->Pins)
			{
				if (Pin && Pin->Direction == EGPD_Output)
				{
					OutputPin = Pin;
					break;
				}
			}

			if (InputPin && OutputPin)
			{
				OutputPin->MakeLinkTo(InputPin);
				++Result.LinksRestored;
			}
		}

		// Lay the chain out in evaluation order rather than declaration order,
		// so the imported graph reads left to right.
		int32 LayoutColumn = 0;
		TSet<int32> Placed;
		for (const TPair<int32, UAnimGraphNode_Base*>& Pair : NodesByLinkIndex)
		{
			// Walk to the head of this node's chain, then place forwards.
			int32 Head = Pair.Key;
			int32 Guard = 0;
			while (NodesByLinkIndex.Contains(SourceLinkByLinkIndex.FindRef(Head)) && Guard++ < NodePropertyOrder.Num())
			{
				Head = SourceLinkByLinkIndex.FindRef(Head);
			}

			for (int32 Current = Head, Step = 0; Step <= NodePropertyOrder.Num(); ++Step)
			{
				if (Placed.Contains(Current))
				{
					break;
				}

				Placed.Add(Current);
				NodesByLinkIndex[Current]->NodePosX = LayoutColumn++ * 400;

				// Find whichever node consumes this one.
				int32 Next = INDEX_NONE;
				for (const TPair<int32, int32>& Link : SourceLinkByLinkIndex)
				{
					if (Link.Value == Current && NodesByLinkIndex.Contains(Link.Key))
					{
						Next = Link.Key;
						break;
					}
				}

				if (Next == INDEX_NONE)
				{
					break;
				}
				Current = Next;
			}
		}
	}

	Result.UnresolvedAssetReferences = Context.UnresolvedReferences;
	Result.CreatedStubAssets = Context.CreatedStubAssets;
	Result.ExternalForcesRebuilt = Context.ExternalForcesRebuilt;

	Result.bSuccess = true;
	Result.Message = FString::Printf(
		TEXT("Imported %d Kawaii Physics node(s), restored %d link(s)."),
		Result.NodesCreated, Result.LinksRestored);

	if (Result.ExternalForcesRebuilt > 0)
	{
		Result.Message += FString::Printf(TEXT(" Recovered %d external force preset(s)."),
		                                  Result.ExternalForcesRebuilt);
	}

	if (Result.UnresolvedAssetReferences.Num() > 0)
	{
		Result.Message += FString::Printf(TEXT(" %d asset reference(s) could not be resolved and were left empty."),
		                                  Result.UnresolvedAssetReferences.Num());
	}

	if (Result.CreatedStubAssets.Num() > 0)
	{
		Result.Message += FString::Printf(
			TEXT(" Created %d placeholder asset(s) at the game's own paths so the references cook correctly"
				" -- EXCLUDE THESE WHEN PACKING."),
			Result.CreatedStubAssets.Num());
	}

	if (Result.SkippedNodeTypes.Num() > 0)
	{
		// Naming them matters: the user is looking at a graph that does not match
		// the one they exported, and this is the reason why.
		Result.Message += FString::Printf(TEXT(" Skipped non-Kawaii node type(s): %s."),
		                                  *FString::Join(Result.SkippedNodeTypes, TEXT(", ")));
	}

	UE_LOG(LogKawaiiPhysicsEd, Log, TEXT("KawaiiPhysics import: %s"), *Result.Message);

	return Result;
}


FString UKawaiiPhysicsJsonImporter::ConvertJsonToNodeText(const FString& JsonText, USkeleton* SkeletonForStubs,
                                                          bool bCreateStubAssets,
                                                          FKawaiiPhysicsJsonImportResult& OutResult)
{
	// Anim graph nodes resolve their owning Blueprint by walking their outer
	// chain (FindBlueprintForNodeChecked asserts if there is none), so the
	// scratch graph has to hang off a real -- if transient -- Anim Blueprint.
	FKawaiiPhysicsJsonImportResult Result;

	UAnimBlueprint* ScratchBlueprint = NewObject<UAnimBlueprint>(GetTransientPackage(), NAME_None, RF_Transient);
	ScratchBlueprint->TargetSkeleton = SkeletonForStubs;

	UEdGraph* ScratchGraph = FBlueprintEditorUtils::CreateNewGraph(
		ScratchBlueprint, TEXT("KawaiiPhysicsScratch"),
		UAnimationGraph::StaticClass(), UAnimationGraphSchema::StaticClass());

	if (!ScratchGraph)
	{
		Result.Message = TEXT("Could not create a scratch graph.");
		OutResult = Result;
		return FString();
	}

	Result = ImportIntoGraph(ScratchGraph, SkeletonForStubs, JsonText, true, bCreateStubAssets);

	if (!Result.bSuccess)
	{
		OutResult = Result;
		return FString();
	}

	TSet<UObject*> NodesToExport;
	for (UEdGraphNode* Node : ScratchGraph->Nodes)
	{
		NodesToExport.Add(Node);
	}

	FString NodeText;
	FEdGraphUtilities::ExportNodesToText(NodesToExport, NodeText);

	if (NodeText.IsEmpty())
	{
		Result.bSuccess = false;
		Result.Message = TEXT("Nodes were built but produced no clipboard text.");
		OutResult = Result;
		return FString();
	}

	Result.Message = FString::Printf(
		TEXT("Converted %d node(s). Paste into an AnimGraph with Ctrl+V."),
		Result.NodesCreated);

	if (Result.CreatedStubAssets.Num() > 0)
	{
		Result.Message += FString::Printf(
			TEXT(" Created %d placeholder asset(s) so references resolve -- EXCLUDE THESE WHEN PACKING."),
			Result.CreatedStubAssets.Num());
	}

	if (Result.ExternalForcesRebuilt > 0)
	{
		Result.Message += FString::Printf(TEXT(" Recovered %d external force preset(s)."),
		                                  Result.ExternalForcesRebuilt);
	}

	if (Result.SkippedNodeTypes.Num() > 0)
	{
		Result.Message += FString::Printf(TEXT(" Skipped non-Kawaii node type(s): %s."),
		                                  *FString::Join(Result.SkippedNodeTypes, TEXT(", ")));
	}

	UE_LOG(LogKawaiiPhysicsEd, Log, TEXT("Kawaii Physics: %s"), *Result.Message);

	OutResult = Result;
	return NodeText;
}

FKawaiiPhysicsJsonImportResult UKawaiiPhysicsJsonImporter::CopyNodesToClipboardFromJson(const FString& JsonText,
	USkeleton* SkeletonForStubs, bool bCreateStubAssets)
{
	FKawaiiPhysicsJsonImportResult Result;
	const FString NodeText = ConvertJsonToNodeText(JsonText, SkeletonForStubs, bCreateStubAssets, Result);

	if (Result.bSuccess && !NodeText.IsEmpty())
	{
		FPlatformApplicationMisc::ClipboardCopy(*NodeText);
	}

	return Result;
}

FKawaiiPhysicsJsonImportResult UKawaiiPhysicsJsonImporter::ImportWithFilePicker(UAnimBlueprint* TargetAnimBlueprint)
{
	FKawaiiPhysicsJsonImportResult Result;

	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		Result.Message = TEXT("No desktop platform available.");
		return Result;
	}

	const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);

	TArray<FString> SelectedFiles;
	const bool bPicked = DesktopPlatform->OpenFileDialog(
		ParentWindowHandle,
		LOCTEXT("ImportDialogTitle", "Select an FModel JSON export of a Physics AnimBlueprint").ToString(),
		TEXT(""), TEXT(""), TEXT("JSON files (*.json)|*.json"),
		EFileDialogFlags::None, SelectedFiles);

	if (!bPicked || SelectedFiles.Num() == 0)
	{
		Result.Message = TEXT("Cancelled.");
		return Result;
	}

	return ImportFromJsonFile(TargetAnimBlueprint, SelectedFiles[0], true, true);
}

#undef LOCTEXT_NAMESPACE
