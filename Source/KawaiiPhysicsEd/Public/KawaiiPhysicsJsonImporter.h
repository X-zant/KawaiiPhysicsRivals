// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "KawaiiPhysicsJsonImporter.generated.h"

class UAnimBlueprint;
class UAnimGraphNode_KawaiiPhysics;
class UEdGraph;

/** What an import did, so callers can report it without re-deriving anything. */
USTRUCT(BlueprintType)
struct FKawaiiPhysicsJsonImportResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Import")
	bool bSuccess = false;

	/** Nodes actually created in the target graph. */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Import")
	int32 NodesCreated = 0;

	/** Pose links reconnected between the created nodes. */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Import")
	int32 LinksRestored = 0;

	/** Object references the JSON named that could not be resolved in this project. */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Import")
	TArray<FString> UnresolvedAssetReferences;

	/**
	 * Anim node types present in the export that this importer does not handle.
	 *
	 * Physics AnimBlueprints in the game routinely mix Kawaii Physics with stock
	 * engine nodes (Pose Driver, Constraint, pose-space conversions). Those are
	 * not ours to recreate, but staying silent about them would leave the user
	 * with a partial graph and no indication why.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Import")
	TArray<FString> SkippedNodeTypes;

	/**
	 * Placeholder assets created so references serialise at the game's own paths.
	 *
	 * These exist only to make the reference cook correctly -- the packaged game
	 * loads its own copy. EXCLUDE THEM WHEN PACKING: shipping a stub at the same
	 * path as the game's real asset can override it with an empty one.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Import")
	TArray<FString> CreatedStubAssets;

	/** External force presets (gravity/wind/curve/basic) recovered from the export. */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Import")
	int32 ExternalForcesRebuilt = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Import")
	FString Message;
};

/**
 * Everything the sanitiser needs in order to turn a cooked object reference back
 * into a live one.
 */
struct FKawaiiPhysicsImportContext
{
	/** Skeleton given to any stub that needs one. Content is irrelevant to a stub. */
	class USkeleton* Skeleton = nullptr;

	/** When false, unresolvable references are dropped instead of stubbed. */
	bool bCreateStubAssets = false;

	TArray<FString> UnresolvedReferences;
	TArray<FString> CreatedStubAssets;

	/** External force presets rebuilt from their untagged JSON. */
	int32 ExternalForcesRebuilt = 0;
};

/**
 * Rebuilds Kawaii Physics setups from a cooked AnimBlueprint's JSON export.
 *
 * The workflow this serves: a modder exports `Post_<SkinID>_<CharID>_Physics`
 * from the packaged game with FModel, and wants the same chains back as real,
 * editable AnimGraph nodes.
 *
 * This is only possible because the plugin's reflected schema matches the
 * game's exactly -- FModel writes property names, and they land straight on the
 * matching UPROPERTY. See README §1.
 */
UCLASS()
class KAWAIIPHYSICSED_API UKawaiiPhysicsJsonImporter : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Imports every Kawaii Physics node found in the JSON into TargetAnimBlueprint's AnimGraph.
	 *
	 * @param TargetAnimBlueprint  Blueprint to add nodes to. Must have an AnimGraph.
	 * @param JsonFilePath         An FModel JSON export of the cooked AnimBlueprint.
	 * @param bRestoreNodeLinks    Recreate the original pose-link order between the imported nodes.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Import")
	static FKawaiiPhysicsJsonImportResult ImportFromJsonFile(UAnimBlueprint* TargetAnimBlueprint,
	                                                         const FString& JsonFilePath,
	                                                         bool bRestoreNodeLinks = true,
	                                                         bool bCreateStubAssets = true);

	/** As above, from a JSON string already in memory. */
	static FKawaiiPhysicsJsonImportResult ImportFromJsonString(UAnimBlueprint* TargetAnimBlueprint,
	                                                           const FString& JsonText,
	                                                           bool bRestoreNodeLinks = true,
	                                                           bool bCreateStubAssets = true);

	/**
	 * Walks every field in an export and reports the ones with no matching
	 * property on the node struct -- i.e. data the import would drop.
	 *
	 * Exists so "did all of it come across?" is a measurement rather than a
	 * belief. Deliberate omissions are prefixed [intentional].
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Import")
	static TArray<FString> AuditJsonCoverage(const FString& JsonFilePath);

	/**
	 * Converts an export into UE's node clipboard format (T3D) and copies it.
	 *
	 * The nodes are built in a throwaway graph and handed to UE's own
	 * FEdGraphUtilities::ExportNodesToText, so the text is generated by the
	 * engine rather than hand-formatted -- it stays correct as the node struct
	 * changes. Needs no skeleton, so it works before the mesh is extracted.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Import")
	static FKawaiiPhysicsJsonImportResult CopyNodesToClipboardFromJson(const FString& JsonText,
	                                                                   class USkeleton* SkeletonForStubs = nullptr,
	                                                                   bool bCreateStubAssets = true);

	/**
	 * Same conversion as above but returns the T3D text instead of copying it,
	 * so other tooling can use it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Import")
	static FString ConvertJsonToNodeText(const FString& JsonText, class USkeleton* SkeletonForStubs,
	                                     bool bCreateStubAssets, FKawaiiPhysicsJsonImportResult& OutResult);

	/** Imports into any graph. Used for both a real AnimGraph and the clipboard path. */
	static FKawaiiPhysicsJsonImportResult ImportIntoGraph(UEdGraph* AnimGraph, class USkeleton* SkeletonForStubs,
	                                                      const FString& JsonText, bool bRestoreNodeLinks,
	                                                      bool bCreateStubAssets);

	/** Opens a file picker and imports into the given Blueprint. Used by the asset context menu. */
	static FKawaiiPhysicsJsonImportResult ImportWithFilePicker(UAnimBlueprint* TargetAnimBlueprint);

	/**
	 * Turns one FModel node object into a runtime node.
	 *
	 * Public because this is the part with real logic in it -- enum qualifiers,
	 * dropped object references, stripped runtime state -- and it is worth
	 * testing without needing an AnimBlueprint to import into.
	 *
	 * NOTE: mutates NodeObject in place.
	 */
	static bool ParseKawaiiNode(const TSharedPtr<class FJsonObject>& NodeObject,
	                            struct FAnimNode_KawaiiPhysics& OutNode,
	                            TArray<FString>& OutUnresolvedReferences);

	/** As above, for any node struct. NodeObject is mutated in place. */
	static bool ParseNodeStruct(const TSharedPtr<class FJsonObject>& NodeObject,
	                            const UScriptStruct* NodeStruct, void* OutNode,
	                            TArray<FString>& OutUnresolvedReferences);

	/** As above, with a context that can resolve or stub asset references. */
	static bool ParseNodeStructWithContext(const TSharedPtr<class FJsonObject>& NodeObject,
	                                       const UScriptStruct* NodeStruct, void* OutNode,
	                                       FKawaiiPhysicsImportContext& Context);

private:
	/** Finds the graph named "AnimGraph" on the Blueprint. */
	static UEdGraph* FindAnimGraph(const UAnimBlueprint* AnimBlueprint);

	/**
	 * Strips the parts of an FModel node object that cannot be deserialised.
	 *
	 * Removes pose links and runtime-only arrays, rewrites "EEnum::Value" to
	 * "Value", and resolves or drops object references. Returns the names of
	 * references that could not be resolved.
	 */
	static void SanitiseNodeJson(const TSharedPtr<class FJsonObject>& NodeObject,
	                             FKawaiiPhysicsImportContext& Context);
};
