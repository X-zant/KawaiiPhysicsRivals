// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License

#include "KawaiiPhysicsJsonImporter.h"

#include "AnimGraphNode_KawaiiPhysics.h"
#include "AnimGraphNode_KawaiiPhysicsMerge.h"
#include "AnimNode_KawaiiPhysics.h"
#include "AnimNode_KawaiiPhysicsMerge.h"
#include "KawaiiPhysicsExternalForce.h"
#include "KawaiiPhysicsEd.h"

#include "AnimGraphNode_Constraint.h"
#include "AnimGraphNode_CopyBone.h"
#include "AnimGraphNode_ModifyBone.h"
#include "AnimGraphNode_ApplyAdditive.h"
#include "AnimGraphNode_BlendBoneByChannel.h"
#include "AnimGraphNode_BlendListByInt.h"
#include "AnimGraphNode_ComponentToLocalSpace.h"
#include "AnimGraphNode_LayeredBoneBlend.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "AnimGraphNode_PoseDriver.h"
#include "AnimGraphNode_SaveCachedPose.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_UseCachedPose.h"
#include "AnimGraphNode_SpringBone.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/PoseAsset.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AnimNodes/AnimNode_PoseDriver.h"
#include "BoneControllers/AnimNode_Constraint.h"
#include "BoneControllers/AnimNode_CopyBone.h"
#include "BoneControllers/AnimNode_ModifyBone.h"
#include "BoneControllers/AnimNode_SpringBone.h"
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
	/**
	 * Every anim node struct is named FAnimNode_*, which is what makes a class
	 * property a node. Property NAMES are not reliable for this -- see the note
	 * where the node list is built.
	 */
	const TCHAR* AnimNodeStructPrefix = TEXT("AnimNode_");

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

	/** One pose input on a node: the pin it arrives on and the node it comes from. */
	struct FPoseInput
	{
		FName PinName;
		int32 SourceLinkIndex = INDEX_NONE;
	};

	/**
	 * Reads every pose input a node has and strips its LinkID from the JSON.
	 *
	 * An input is any field holding an object with a LinkID -- ComponentPose,
	 * LocalPose, SourcePose, Base/Additive, A/B, Pose... -- or an array of
	 * them (BlendPose, BlendPoses). Pin names follow the engine's rule
	 * (UAnimGraphNode_Base::CreatePinsForPoseLink): the property name, with
	 * "_<index>" for an array element.
	 *
	 * The links are graph topology and are restored as pin connections, so
	 * they are removed from the data: a single input is dropped outright, and
	 * an array keeps its entries (emptied) because its LENGTH is what makes the
	 * graph node create that many input pins.
	 *
	 * A use-cached-pose node's LinkToCachingNode is not a pin -- it names the
	 * save node it reads -- and is returned separately.
	 */
	void CollectPoseInputs(const TSharedPtr<FJsonObject>& NodeObject, TArray<FPoseInput>& OutInputs,
	                       int32& OutCacheLinkIndex)
	{
		OutCacheLinkIndex = INDEX_NONE;

		auto ReadLinkId = [](const TSharedPtr<FJsonValue>& Value, int32& OutLinkId)
		{
			if (!Value.IsValid() || Value->Type != EJson::Object)
			{
				return false;
			}
			return Value->AsObject()->TryGetNumberField(TEXT("LinkID"), OutLinkId);
		};

		TArray<FString> Keys;
		NodeObject->Values.GetKeys(Keys);
		for (const FString& Key : Keys)
		{
			const TSharedPtr<FJsonValue> Value = NodeObject->Values.FindRef(Key);

			int32 LinkId = INDEX_NONE;
			if (ReadLinkId(Value, LinkId))
			{
				if (Key == TEXT("LinkToCachingNode"))
				{
					OutCacheLinkIndex = LinkId;
				}
				else
				{
					OutInputs.Add({FName(*Key), LinkId});
				}
				NodeObject->RemoveField(Key);
				continue;
			}

			if (!Value.IsValid() || Value->Type != EJson::Array)
			{
				continue;
			}

			const TArray<TSharedPtr<FJsonValue>>& Elements = Value->AsArray();
			TArray<TSharedPtr<FJsonValue>> Emptied;
			bool bIsPoseArray = false;
			for (int32 Index = 0; Index < Elements.Num(); ++Index)
			{
				if (ReadLinkId(Elements[Index], LinkId))
				{
					bIsPoseArray = true;
					OutInputs.Add({FName(*FString::Printf(TEXT("%s_%d"), *Key, Index)), LinkId});
					Emptied.Add(MakeShared<FJsonValueObject>(MakeShared<FJsonObject>()));
				}
				else
				{
					Emptied.Add(Elements[Index]);
				}
			}
			if (bIsPoseArray)
			{
				NodeObject->SetArrayField(Key, Emptied);
			}
		}
	}

	/**
	 * Recovers each node's FOLDED properties.
	 *
	 * Anim node properties marked FoldProperty (blend times and types, sequence
	 * player settings...) are moved out of the node when the AnimBlueprint is
	 * compiled, into one constant-data struct shared by the class. The export
	 * keeps the map back: AnimNodeData[node].Entries[slot] is an index into the
	 * constant-data struct's properties (0xFFFFFFFF if that property is not
	 * folded), where slot comes from NodeTypeMap[node struct]. The values
	 * themselves are in the defaults' SerializedSparseClassData. NodeIndex is
	 * the node's position in the class's anim-node list -- the same order
	 * LinkID uses.
	 *
	 * Returns, per node index, an object of { property name: value }.
	 */
	TArray<TSharedPtr<FJsonObject>> BuildFoldedValues(const TSharedPtr<FJsonObject>& ClassExport,
	                                                  const TSharedPtr<FJsonObject>& DefaultsExport,
	                                                  const TSharedPtr<FJsonObject>& ConstantDataExport,
	                                                  const TArray<FString>& NodeStructTypes)
	{
		TArray<TSharedPtr<FJsonObject>> Folded;
		for (int32 Index = 0; Index < NodeStructTypes.Num(); ++Index)
		{
			Folded.Add(MakeShared<FJsonObject>());
		}

		const TSharedPtr<FJsonObject>* ClassProperties = nullptr;
		const TSharedPtr<FJsonObject>* Sparse = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* ConstantChildren = nullptr;
		if (!ConstantDataExport.IsValid() ||
			!ClassExport->TryGetObjectField(TEXT("Properties"), ClassProperties) ||
			!DefaultsExport->TryGetObjectField(TEXT("SerializedSparseClassData"), Sparse) ||
			!ConstantDataExport->TryGetArrayField(TEXT("ChildProperties"), ConstantChildren))
		{
			return Folded;
		}

		TArray<FString> ConstantNames;
		for (const TSharedPtr<FJsonValue>& Child : *ConstantChildren)
		{
			FString Name;
			if (Child.IsValid() && Child->Type == EJson::Object)
			{
				Child->AsObject()->TryGetStringField(TEXT("Name"), Name);
			}
			ConstantNames.Add(Name);
		}

		// "Class'/Script/Engine.AnimNode_Root'" -> { "Result": 3, "Name": 4, ... }
		TMap<FString, TMap<FString, int32>> SlotsByStruct;
		const TArray<TSharedPtr<FJsonValue>>* TypeMap = nullptr;
		if ((*ClassProperties)->TryGetArrayField(TEXT("NodeTypeMap"), TypeMap))
		{
			for (const TSharedPtr<FJsonValue>& Entry : *TypeMap)
			{
				const TSharedPtr<FJsonObject> EntryObject = Entry->AsObject();
				FString Key = EntryObject->GetStringField(TEXT("Key"));
				int32 Dot = INDEX_NONE;
				Key.FindLastChar(TEXT('.'), Dot);
				Key = Key.Mid(Dot + 1).Replace(TEXT("'"), TEXT(""));

				TMap<FString, int32>& Slots = SlotsByStruct.Add(Key);
				const TSharedPtr<FJsonObject>* MapValue = nullptr;
				const TArray<TSharedPtr<FJsonValue>>* NameToIndex = nullptr;
				if (EntryObject->TryGetObjectField(TEXT("Value"), MapValue) &&
					(*MapValue)->TryGetArrayField(TEXT("NameToIndexMap"), NameToIndex))
				{
					for (const TSharedPtr<FJsonValue>& Pair : *NameToIndex)
					{
						Slots.Add(Pair->AsObject()->GetStringField(TEXT("Key")),
						          static_cast<int32>(Pair->AsObject()->GetNumberField(TEXT("Value"))));
					}
				}
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* NodeData = nullptr;
		if (!(*ClassProperties)->TryGetArrayField(TEXT("AnimNodeData"), NodeData))
		{
			return Folded;
		}

		for (const TSharedPtr<FJsonValue>& DataValue : *NodeData)
		{
			const TSharedPtr<FJsonObject> Data = DataValue->AsObject();
			const int32 NodeIndex = static_cast<int32>(Data->GetNumberField(TEXT("NodeIndex")));
			const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
			if (!NodeStructTypes.IsValidIndex(NodeIndex) || !Data->TryGetArrayField(TEXT("Entries"), Entries))
			{
				continue;
			}

			const TMap<FString, int32>* Slots = SlotsByStruct.Find(NodeStructTypes[NodeIndex]);
			if (!Slots)
			{
				continue;
			}

			for (const TPair<FString, int32>& Slot : *Slots)
			{
				// The node function hooks are folded too, but are not settings.
				if (Slot.Key.EndsWith(TEXT("Function")) || !Entries->IsValidIndex(Slot.Value))
				{
					continue;
				}

				const double ConstantIndex = (*Entries)[Slot.Value]->AsNumber();
				if (ConstantIndex < 0 || ConstantIndex >= ConstantNames.Num())
				{
					continue; // 0xFFFFFFFF: not folded, the value is on the node itself
				}

				if (const TSharedPtr<FJsonValue> Value =
					(*Sparse)->Values.FindRef(ConstantNames[static_cast<int32>(ConstantIndex)]))
				{
					Folded[NodeIndex]->SetField(Slot.Key, Value);
				}
			}
		}

		return Folded;
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
		if (UAnimationAsset* AnimationAsset = Cast<UAnimationAsset>(Stub))
		{
			// Pose assets and sequences (a Sequence Player's animation) cannot be
			// opened without a skeleton.
			if (FallbackSkeleton)
			{
				AnimationAsset->SetSkeleton(FallbackSkeleton);
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
			const FString& Text = Value->AsString();

			// JSON has no NaN/Infinity, so some writers emit them as strings
			// (CUE4Parse/Newtonsoft does, for an uninitialised float). Drop the
			// field and keep the default rather than fail the conversion.
			if (Text == TEXT("NaN") || Text == TEXT("Infinity") || Text == TEXT("-Infinity"))
			{
				return nullptr;
			}

			// FModel writes an FGuid as four 8-digit groups ("C34B2A45-498A6B7F-
			// A4CDD79D-F3D94FAC"), which FGuid's parser rejects -- and one bad
			// field aborts the whole node. Drop the hyphens: 32 hex digits is a
			// form it accepts.
			if (Text.Len() == 35 && Text[8] == TEXT('-') && Text[17] == TEXT('-') && Text[26] == TEXT('-'))
			{
				const FString Digits = Text.Replace(TEXT("-"), TEXT(""));
				FGuid Parsed;
				if (FGuid::Parse(Digits, Parsed))
				{
					return MakeShared<FJsonValueString>(Digits);
				}
			}

			FString Stripped;
			if (TryStripEnumQualifier(Text, Stripped))
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
	// Holds folded node properties (see BuildFoldedValues). Optional.
	TSharedPtr<FJsonObject> ConstantDataExport;

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
		else if (Name == TEXT("AnimBlueprintGeneratedConstantData"))
		{
			ConstantDataExport = Export;
		}
	}

	if (!ClassExport.IsValid() || !DefaultsExport.IsValid())
	{
		Result.Message = TEXT("Export does not contain an AnimBlueprintGeneratedClass and its defaults. "
			"Export the whole cooked AnimBlueprint package from FModel, not a single object.");
		return Result;
	}

	// LinkID indexes the class's anim-node properties in declaration order, so
	// this list has to contain every node property and nothing else.
	//
	// A node is identified by the struct behind it, NOT by its property name.
	// The compiler usually names them "AnimGraphNode_<Type>_<N>", but a node
	// given a name in the editor keeps that name instead -- the game ships
	// several, e.g. "RivetConstraint_1" for an FAnimNode_Constraint in
	// Post_1014102_Lobby_Physics. Matching on the name prefix would drop those,
	// and dropping one shifts every LinkID after it, silently rewiring the graph.
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

			const FString StructTypeName = ReadStructTypeName(ChildValue->AsObject());
			if (!StructTypeName.StartsWith(AnimNodeStructPrefix))
			{
				continue;
			}

			FString ChildName;
			ChildValue->AsObject()->TryGetStringField(TEXT("Name"), ChildName);

			NodePropertyOrder.Add(ChildName);
			NodeStructTypes.Add(StructTypeName);
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

	// Folded properties live in the class's constant data, not on the node.
	const TArray<TSharedPtr<FJsonObject>> FoldedValues =
		BuildFoldedValues(ClassExport, DefaultsExport, ConstantDataExport, NodeStructTypes);

	// The whole graph between the AnimBlueprint's input and output is the game's
	// setup, so all of it is recreated. Only the two ends are not: the output
	// (Root) and the linked input pose already exist in the graph this is pasted
	// into, and joining them up is the modder's call.
	auto IsGraphEnd = [](const FString& StructType)
	{
		return StructType == TEXT("AnimNode_Root") || StructType == TEXT("AnimNode_LinkedInputPose");
	};

	// A save-cached-pose node nothing reads is dead in the game too; recreating
	// it would only add a nameless cache. Find which ones are actually used.
	TSet<int32> UsedCacheIndices;
	for (int32 LinkIndex = 0; LinkIndex < NodePropertyOrder.Num(); ++LinkIndex)
	{
		const TSharedPtr<FJsonObject>* NodeObjectPtr = nullptr;
		const TSharedPtr<FJsonObject>* CacheLink = nullptr;
		int32 CacheIndex = INDEX_NONE;
		if (NodeStructTypes[LinkIndex] == TEXT("AnimNode_UseCachedPose") &&
			(*Properties)->TryGetObjectField(NodePropertyOrder[LinkIndex], NodeObjectPtr) &&
			(*NodeObjectPtr)->TryGetObjectField(TEXT("LinkToCachingNode"), CacheLink) &&
			(*CacheLink)->TryGetNumberField(TEXT("LinkID"), CacheIndex))
		{
			UsedCacheIndices.Add(CacheIndex);
		}
	}

	TMap<int32, UAnimGraphNode_Base*> NodesByLinkIndex;
	TMap<int32, TArray<FPoseInput>> InputsByLinkIndex;
	TMap<int32, int32> CacheLinkByLinkIndex;
	bool bHasGraphEnds = false;

	for (int32 LinkIndex = 0; LinkIndex < NodePropertyOrder.Num(); ++LinkIndex)
	{
		const FString& PropertyName = NodePropertyOrder[LinkIndex];
		const FString& StructType = NodeStructTypes[LinkIndex];

		if (IsGraphEnd(StructType))
		{
			bHasGraphEnds = true;
			continue;
		}

		const TSharedPtr<FJsonObject>* NodeObjectPtr = nullptr;
		if (!(*Properties)->TryGetObjectField(PropertyName, NodeObjectPtr) || !NodeObjectPtr)
		{
			continue;
		}

		// Work on a copy: folded values are merged in and inputs stripped out.
		const TSharedPtr<FJsonObject> NodeObject = MakeShared<FJsonObject>(**NodeObjectPtr);
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : FoldedValues[LinkIndex]->Values)
		{
			if (!NodeObject->HasField(Pair.Key))
			{
				NodeObject->SetField(Pair.Key, Pair.Value);
			}
		}

		if (StructType == TEXT("AnimNode_SaveCachedPose") && !UsedCacheIndices.Contains(LinkIndex))
		{
			continue;
		}

		FString CacheName;
		NodeObject->TryGetStringField(TEXT("CachePoseName"), CacheName);

		TArray<FPoseInput> Inputs;
		int32 CacheLinkIndex = INDEX_NONE;
		CollectPoseInputs(NodeObject, Inputs, CacheLinkIndex);

		UAnimGraphNode_Base* GraphNode = nullptr;
		bool bRecognised = true;

		// One shape per node type: convert into the runtime struct, place the
		// matching graph node, THEN hand it the struct and rebuild its pins.
		// Order matters: placing a node runs its own setup -- a Blend List adds a
		// pose of its own -- which would otherwise land on top of the imported
		// data. Rebuilding allocates pins from the imported struct, so array
		// inputs (blend poses) get exactly one pin per element.
		#define KAWAIIPHYSICS_IMPORT_NODE(RuntimeType, GraphType, FixUp) \
			{ \
				RuntimeType ImportedNode; \
				if (!ParseNodeStructWithContext(NodeObject, RuntimeType::StaticStruct(), &ImportedNode, Context)) \
				{ \
					UE_LOG(LogKawaiiPhysicsEd, Warning, \
					       TEXT("KawaiiPhysics import: could not convert '%s'; skipped."), *PropertyName); \
					continue; \
				} \
				FixUp \
				FGraphNodeCreator<GraphType> NodeCreator(*AnimGraph); \
				GraphType* NewNode = NodeCreator.CreateNode(); \
				NodeCreator.Finalize(); \
				NewNode->Node = ImportedNode; \
				NewNode->ReconstructNode(); \
				GraphNode = NewNode; \
			}

		if (StructType == TEXT("AnimNode_KawaiiPhysics"))
		{
			KAWAIIPHYSICS_IMPORT_NODE(FAnimNode_KawaiiPhysics, UAnimGraphNode_KawaiiPhysics, ;)
		}
		else if (StructType == TEXT("AnimNode_KawaiiPhysicsMerge"))
		{
			KAWAIIPHYSICS_IMPORT_NODE(FAnimNode_KawaiiPhysicsMerge, UAnimGraphNode_KawaiiPhysicsMerge, ;)
		}
		else if (StructType == TEXT("AnimNode_PoseDriver"))
		{
			KAWAIIPHYSICS_IMPORT_NODE(FAnimNode_PoseDriver, UAnimGraphNode_PoseDriver, ;)
		}
		else if (StructType == TEXT("AnimNode_Constraint"))
		{
			KAWAIIPHYSICS_IMPORT_NODE(FAnimNode_Constraint, UAnimGraphNode_Constraint, ;)
		}
		else if (StructType == TEXT("AnimNode_ModifyBone"))
		{
			KAWAIIPHYSICS_IMPORT_NODE(FAnimNode_ModifyBone, UAnimGraphNode_ModifyBone, ;)
		}
		else if (StructType == TEXT("AnimNode_CopyBone"))
		{
			KAWAIIPHYSICS_IMPORT_NODE(FAnimNode_CopyBone, UAnimGraphNode_CopyBone, ;)
		}
		else if (StructType == TEXT("AnimNode_SpringBone"))
		{
			KAWAIIPHYSICS_IMPORT_NODE(FAnimNode_SpringBone, UAnimGraphNode_SpringBone, ;)
		}
		else if (StructType == TEXT("AnimNode_ConvertLocalToComponentSpace"))
		{
			KAWAIIPHYSICS_IMPORT_NODE(FAnimNode_ConvertLocalToComponentSpace, UAnimGraphNode_LocalToComponentSpace, ;)
		}
		else if (StructType == TEXT("AnimNode_ConvertComponentToLocalSpace"))
		{
			KAWAIIPHYSICS_IMPORT_NODE(FAnimNode_ConvertComponentToLocalSpace, UAnimGraphNode_ComponentToLocalSpace, ;)
		}
		else if (StructType == TEXT("AnimNode_BlendListByInt"))
		{
			// One blend time per input (the engine's default 0.1, as AddPose uses)
			// if the folded value was absent. Done on the JSON: the node's arrays
			// are not publicly writable.
			const TArray<TSharedPtr<FJsonValue>>* BlendPoses = nullptr;
			if (NodeObject->TryGetArrayField(TEXT("BlendPose"), BlendPoses))
			{
				TArray<TSharedPtr<FJsonValue>> BlendTimes;
				const TArray<TSharedPtr<FJsonValue>>* Existing = nullptr;
				if (NodeObject->TryGetArrayField(TEXT("BlendTime"), Existing))
				{
					BlendTimes = *Existing;
				}
				while (BlendTimes.Num() < BlendPoses->Num())
				{
					BlendTimes.Add(MakeShared<FJsonValueNumber>(0.1));
				}
				NodeObject->SetArrayField(TEXT("BlendTime"), BlendTimes);
			}
			KAWAIIPHYSICS_IMPORT_NODE(FAnimNode_BlendListByInt, UAnimGraphNode_BlendListByInt, ;)
		}
		else if (StructType == TEXT("AnimNode_LayeredBoneBlend"))
		{
			// One weight and one layer description per blended pose.
			KAWAIIPHYSICS_IMPORT_NODE(FAnimNode_LayeredBoneBlend, UAnimGraphNode_LayeredBoneBlend,
			                          ImportedNode.BlendWeights.SetNum(ImportedNode.BlendPoses.Num());
			                          ImportedNode.LayerSetup.SetNum(ImportedNode.BlendPoses.Num());)
		}
		else if (StructType == TEXT("AnimNode_ApplyAdditive"))
		{
			KAWAIIPHYSICS_IMPORT_NODE(FAnimNode_ApplyAdditive, UAnimGraphNode_ApplyAdditive, ;)
		}
		else if (StructType == TEXT("AnimNode_BlendBoneByChannel"))
		{
			// Its graph node keeps the struct in BlendNode, not Node.
			FAnimNode_BlendBoneByChannel ImportedNode;
			if (!ParseNodeStructWithContext(NodeObject, FAnimNode_BlendBoneByChannel::StaticStruct(), &ImportedNode,
			                                Context))
			{
				UE_LOG(LogKawaiiPhysicsEd, Warning, TEXT("KawaiiPhysics import: could not convert '%s'; skipped."),
				       *PropertyName);
				continue;
			}
			FGraphNodeCreator<UAnimGraphNode_BlendBoneByChannel> NodeCreator(*AnimGraph);
			UAnimGraphNode_BlendBoneByChannel* NewNode = NodeCreator.CreateNode();
			NodeCreator.Finalize();
			NewNode->BlendNode = ImportedNode;
			NewNode->ReconstructNode();
			GraphNode = NewNode;
		}
		else if (StructType == TEXT("AnimNode_SequencePlayer"))
		{
			KAWAIIPHYSICS_IMPORT_NODE(FAnimNode_SequencePlayer, UAnimGraphNode_SequencePlayer, ;)
		}
		else if (StructType == TEXT("AnimNode_SaveCachedPose"))
		{
			KAWAIIPHYSICS_IMPORT_NODE(FAnimNode_SaveCachedPose, UAnimGraphNode_SaveCachedPose, ;)
			// Set after creation: placing a new save node gives it a default name.
			if (UAnimGraphNode_SaveCachedPose* SaveNode = Cast<UAnimGraphNode_SaveCachedPose>(GraphNode))
			{
				SaveNode->CacheName = CacheName;
			}
		}
		else if (StructType == TEXT("AnimNode_UseCachedPose"))
		{
			KAWAIIPHYSICS_IMPORT_NODE(FAnimNode_UseCachedPose, UAnimGraphNode_UseCachedPose, ;)
		}
		else
		{
			bRecognised = false;
		}

		#undef KAWAIIPHYSICS_IMPORT_NODE

		if (!bRecognised)
		{
			if (!StructType.IsEmpty())
			{
				Result.SkippedNodeTypes.AddUnique(StructType);
			}
			continue;
		}

		NodesByLinkIndex.Add(LinkIndex, GraphNode);
		InputsByLinkIndex.Add(LinkIndex, MoveTemp(Inputs));
		if (CacheLinkIndex != INDEX_NONE)
		{
			CacheLinkByLinkIndex.Add(LinkIndex, CacheLinkIndex);
		}
		++Result.NodesCreated;
	}

	if (Result.NodesCreated == 0)
	{
		Result.Message = TEXT("Nothing importable in that export. Recognised: Kawaii Physics (+ Merge), Pose Driver, "
		                      "Constraint, Transform (Modify) Bone, Copy Bone, Spring Bone, space conversions, "
		                      "Blend Poses by Int, Layered Blend per Bone, Apply Additive, Blend Bone by Channel, "
		                      "Sequence Player, and cached poses.");
		return Result;
	}

	// Cached poses are joined by reference, not by a pin: point each use node at
	// its save node. Asking for the title fills in the cache name it shows and
	// carries through a copy/paste.
	for (const TPair<int32, int32>& CacheLink : CacheLinkByLinkIndex)
	{
		UAnimGraphNode_UseCachedPose* UseNode = Cast<UAnimGraphNode_UseCachedPose>(NodesByLinkIndex.FindRef(CacheLink.Key));
		UAnimGraphNode_SaveCachedPose* SaveNode =
			Cast<UAnimGraphNode_SaveCachedPose>(NodesByLinkIndex.FindRef(CacheLink.Value));
		if (UseNode && SaveNode)
		{
			UseNode->SaveCachedPoseNode = SaveNode;
			UseNode->GetNodeTitle(ENodeTitleType::FullTitle);
		}
	}

	if (bRestoreNodeLinks)
	{
		// Rebuild every pose connection. An input whose source was not recreated
		// -- the graph's input pose, or an unsupported node -- is left open.
		for (const TPair<int32, TArray<FPoseInput>>& NodeInputs : InputsByLinkIndex)
		{
			UAnimGraphNode_Base* TargetNode = NodesByLinkIndex.FindRef(NodeInputs.Key);
			for (const FPoseInput& Input : NodeInputs.Value)
			{
				UAnimGraphNode_Base* SourceNode = NodesByLinkIndex.FindRef(Input.SourceLinkIndex);
				if (!TargetNode || !SourceNode)
				{
					continue;
				}

				UEdGraphPin* InputPin = TargetNode->FindPin(Input.PinName, EGPD_Input);
				UEdGraphPin* OutputPin = nullptr;
				for (UEdGraphPin* Pin : SourceNode->Pins)
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
				else
				{
					UE_LOG(LogKawaiiPhysicsEd, Warning, TEXT("KawaiiPhysics import: no pin '%s' to reconnect on %s."),
					       *Input.PinName.ToString(), *TargetNode->GetName());
				}
			}
		}
	}

	// Lay the graph out left to right by depth: each node one column past the
	// deepest node feeding it (a use-cached-pose node counts as fed by its
	// save node). Handles branches and merges, which a single chain walk can't.
	TMap<int32, int32> Depth;
	TFunction<int32(int32, int32)> GetDepth = [&](int32 LinkIndex, int32 Guard) -> int32
	{
		if (const int32* Known = Depth.Find(LinkIndex))
		{
			return *Known;
		}
		int32 MaxDepth = 0;
		if (Guard < NodePropertyOrder.Num())
		{
			TArray<int32> Sources;
			for (const FPoseInput& Input : InputsByLinkIndex.FindRef(LinkIndex))
			{
				Sources.Add(Input.SourceLinkIndex);
			}
			if (const int32* CacheSource = CacheLinkByLinkIndex.Find(LinkIndex))
			{
				Sources.Add(*CacheSource);
			}
			for (const int32 Source : Sources)
			{
				if (NodesByLinkIndex.Contains(Source))
				{
					MaxDepth = FMath::Max(MaxDepth, GetDepth(Source, Guard + 1) + 1);
				}
			}
		}
		Depth.Add(LinkIndex, MaxDepth);
		return MaxDepth;
	};

	TMap<int32, int32> RowsUsed;
	for (const TPair<int32, UAnimGraphNode_Base*>& Pair : NodesByLinkIndex)
	{
		const int32 Column = GetDepth(Pair.Key, 0);
		int32& Row = RowsUsed.FindOrAdd(Column);
		Pair.Value->NodePosX = Column * 400;
		Pair.Value->NodePosY = Row++ * 250;
	}

	Result.UnresolvedAssetReferences = Context.UnresolvedReferences;
	Result.CreatedStubAssets = Context.CreatedStubAssets;
	Result.ExternalForcesRebuilt = Context.ExternalForcesRebuilt;

	Result.bSuccess = true;
	Result.Message = FString::Printf(
		TEXT("Imported %d node(s), restored %d link(s)."),
		Result.NodesCreated, Result.LinksRestored);
	if (bHasGraphEnds)
	{
		Result.Message += TEXT(" Connect your graph's Input Pose and Output Pose to the open ends.");
	}

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
		Result.Message += FString::Printf(TEXT(" Not recreated (graph plumbing or unsupported): %s."),
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
		Result.Message += FString::Printf(TEXT(" Not recreated (graph plumbing or unsupported): %s."),
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
