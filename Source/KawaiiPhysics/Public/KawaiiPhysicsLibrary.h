// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License
//
// Every accessor that reads or writes per-chain state takes a trailing
// ChainIndex, matching the game's reflected signatures. It defaults to 0, so a
// graph authored against the single-chain API keeps working and simply targets
// the first chain.

#pragma once

#include "CoreMinimal.h"
#include "AnimNode_KawaiiPhysics.h"
#include "KawaiiPhysicsExternalForce.h"
#include "Animation/AnimNodeReference.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "KawaiiPhysicsLibrary.generated.h"

class UKawaiiPhysicsLimitsDataAsset;

UENUM()
enum class EKawaiiPhysicsAccessExternalForceResult : uint8
{
	Valid,
	NotValid,
};

/** Writes ChainPath on the chain at ChainIndex, on the anim thread. */
#define KAWAIIPHYSICS_CHAIN_SETTER(PropertyName, ChainPath) \
	{ \
		const int32 LocalChainIndex = ChainIndex; \
		KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>( \
			TEXT("Set" #PropertyName), \
			[PropertyName, LocalChainIndex](FAnimNode_KawaiiPhysics& InKawaiiPhysics) \
			{ \
				if (FKawaiiPhysicsChain* Chain = InKawaiiPhysics.GetChain(LocalChainIndex)) \
				{ \
					Chain->ChainPath = PropertyName; \
				} \
			}); \
		return KawaiiPhysics; \
	}

/** Reads ChainPath off the chain at ChainIndex; returns a default for a bad index. */
#define KAWAIIPHYSICS_CHAIN_GETTER(ValueType, PropertyName, ChainPath) \
	{ \
		ValueType Value = ValueType(); \
		const int32 LocalChainIndex = ChainIndex; \
		KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>( \
			TEXT("Get" #PropertyName), \
			[&Value, LocalChainIndex](FAnimNode_KawaiiPhysics& InKawaiiPhysics) \
			{ \
				if (const FKawaiiPhysicsChain* Chain = InKawaiiPhysics.GetChain(LocalChainIndex)) \
				{ \
					Value = Chain->ChainPath; \
				} \
			}); \
		return Value; \
	}

/** Node-level (not per-chain) accessors. */
#define KAWAIIPHYSICS_VALUE_SETTER(PropertyName) \
	{ \
		KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>( \
			TEXT("Set" #PropertyName), \
			[PropertyName](FAnimNode_KawaiiPhysics& InKawaiiPhysics) { InKawaiiPhysics.PropertyName = PropertyName; }); \
		return KawaiiPhysics; \
	}

#define KAWAIIPHYSICS_VALUE_GETTER(ValueType, PropertyName) \
	{ \
		ValueType Value = ValueType(); \
		KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>( \
			TEXT("Get" #PropertyName), \
			[&Value](FAnimNode_KawaiiPhysics& InKawaiiPhysics) { Value = InKawaiiPhysics.PropertyName; }); \
		return Value; \
	}


USTRUCT(BlueprintType)
struct FKawaiiPhysicsReference : public FAnimNodeReference
{
	GENERATED_BODY()

	using FInternalNodeType = FAnimNode_KawaiiPhysics;
};

UCLASS()
class KAWAIIPHYSICS_API UKawaiiPhysicsLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta = (BlueprintThreadSafe, ExpandEnumAsExecs = "Result"))
	static FKawaiiPhysicsReference ConvertToKawaiiPhysics(const FAnimNodeReference& Node,
	                                                      EAnimNodeReferenceConversionResult& Result);

	UFUNCTION(BlueprintPure, Category = "Kawaii Physics",
		meta = (BlueprintThreadSafe, DisplayName = "Convert to Kawaii Physics (Pure)"))
	static void ConvertToKawaiiPhysicsPure(const FAnimNodeReference& Node, FKawaiiPhysicsReference& KawaiiPhysics,
	                                       bool& Result)
	{
		EAnimNodeReferenceConversionResult ConversionResult;
		KawaiiPhysics = ConvertToKawaiiPhysics(Node, ConversionResult);
		Result = (ConversionResult == EAnimNodeReferenceConversionResult::Succeeded);
	}

	static bool CollectKawaiiPhysicsNodes(TArray<FKawaiiPhysicsReference>& Nodes, UAnimInstance* AnimInstance,
	                                      const FGameplayTagContainer& FilterTags, bool bFilterExactMatch);

	static bool CollectKawaiiPhysicsNodes(TArray<FKawaiiPhysicsReference>& Nodes, USkeletalMeshComponent* MeshComp,
	                                      const FGameplayTagContainer& FilterTags, bool bFilterExactMatch);

	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta = (BlueprintThreadSafe))
	static FKawaiiPhysicsReference ResetDynamics(const FKawaiiPhysicsReference& KawaiiPhysics);

	// --- per-chain bone setup ---

	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta = (BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetRootBoneName(const FKawaiiPhysicsReference& KawaiiPhysics,
	                                               UPARAM(ref) FName& RootBoneName, int32 ChainIndex = 0);

	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta = (BlueprintThreadSafe))
	static FName GetRootBoneName(const FKawaiiPhysicsReference& KawaiiPhysics, int32 ChainIndex = 0);

	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta = (BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetExcludeBoneNames(const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                   UPARAM(ref) TArray<FName>& ExcludeBoneNames,
	                                                   int32 ChainIndex = 0);

	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta = (BlueprintThreadSafe))
	static TArray<FName> GetExcludeBoneNames(const FKawaiiPhysicsReference& KawaiiPhysics, int32 ChainIndex = 0);

	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta = (BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetDummyBoneLength(const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                  float DummyBoneLength, int32 ChainIndex = 0)
	KAWAIIPHYSICS_CHAIN_SETTER(DummyBoneLength, BoneSettings.DummyBoneLength)

	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta = (BlueprintThreadSafe))
	static float GetDummyBoneLength(const FKawaiiPhysicsReference& KawaiiPhysics, int32 ChainIndex = 0)
	KAWAIIPHYSICS_CHAIN_GETTER(float, DummyBoneLength, BoneSettings.DummyBoneLength)

	// --- per-chain physics ---

	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta = (BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetPhysicsSettings(const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                  UPARAM(ref) FKawaiiPhysicsSettings& PhysicsSettings,
	                                                  int32 ChainIndex = 0)
	KAWAIIPHYSICS_CHAIN_SETTER(PhysicsSettings, PhysicsSettings.PhysicsSettings)

	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta = (BlueprintThreadSafe))
	static FKawaiiPhysicsSettings GetPhysicsSettings(const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                 int32 ChainIndex = 0)
	KAWAIIPHYSICS_CHAIN_GETTER(FKawaiiPhysicsSettings, PhysicsSettings, PhysicsSettings.PhysicsSettings)

	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta = (BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetTeleportDistanceThreshold(const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                            float TeleportDistanceThreshold,
	                                                            int32 ChainIndex = 0)
	KAWAIIPHYSICS_CHAIN_SETTER(TeleportDistanceThreshold, PhysicsSettings.TeleportDistanceThreshold)

	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta = (BlueprintThreadSafe))
	static float GetTeleportDistanceThreshold(const FKawaiiPhysicsReference& KawaiiPhysics, int32 ChainIndex = 0)
	KAWAIIPHYSICS_CHAIN_GETTER(float, TeleportDistanceThreshold, PhysicsSettings.TeleportDistanceThreshold)

	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta = (BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetTeleportRotationThreshold(const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                            float TeleportRotationThreshold,
	                                                            int32 ChainIndex = 0)
	KAWAIIPHYSICS_CHAIN_SETTER(TeleportRotationThreshold, PhysicsSettings.TeleportRotationThreshold)

	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta = (BlueprintThreadSafe))
	static float GetTeleportRotationThreshold(const FKawaiiPhysicsReference& KawaiiPhysics, int32 ChainIndex = 0)
	KAWAIIPHYSICS_CHAIN_GETTER(float, TeleportRotationThreshold, PhysicsSettings.TeleportRotationThreshold)

	// --- per-chain external force ---

	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta = (BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetGravity(const FKawaiiPhysicsReference& KawaiiPhysics, FVector Gravity,
	                                          int32 ChainIndex = 0)
	KAWAIIPHYSICS_CHAIN_SETTER(Gravity, ExternalForceSettings.Gravity)

	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta = (BlueprintThreadSafe))
	static FVector GetGravity(const FKawaiiPhysicsReference& KawaiiPhysics, int32 ChainIndex = 0)
	KAWAIIPHYSICS_CHAIN_GETTER(FVector, Gravity, ExternalForceSettings.Gravity)

	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta = (BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetEnableWind(const FKawaiiPhysicsReference& KawaiiPhysics, bool bEnableWind,
	                                             int32 ChainIndex = 0)
	KAWAIIPHYSICS_CHAIN_SETTER(bEnableWind, ExternalForceSettings.bEnableWind)

	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta = (BlueprintThreadSafe))
	static bool GetEnableWind(const FKawaiiPhysicsReference& KawaiiPhysics, int32 ChainIndex = 0)
	KAWAIIPHYSICS_CHAIN_GETTER(bool, bEnableWind, ExternalForceSettings.bEnableWind)

	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta = (BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetWindScale(const FKawaiiPhysicsReference& KawaiiPhysics, float WindScale,
	                                            int32 ChainIndex = 0)
	KAWAIIPHYSICS_CHAIN_SETTER(WindScale, ExternalForceSettings.WindScale)

	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta = (BlueprintThreadSafe))
	static float GetWindScale(const FKawaiiPhysicsReference& KawaiiPhysics, int32 ChainIndex = 0)
	KAWAIIPHYSICS_CHAIN_GETTER(float, WindScale, ExternalForceSettings.WindScale)

	// --- node-level ---

	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta = (BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetAllowWorldCollision(const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                      bool bAllowWorldCollision)
	KAWAIIPHYSICS_VALUE_SETTER(bAllowWorldCollision)

	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta = (BlueprintThreadSafe))
	static bool GetAllowWorldCollision(const FKawaiiPhysicsReference& KawaiiPhysics)
	KAWAIIPHYSICS_VALUE_GETTER(bool, bAllowWorldCollision)

	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta = (BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetNeedWarmUp(const FKawaiiPhysicsReference& KawaiiPhysics, bool bNeedWarmUp)
	KAWAIIPHYSICS_VALUE_SETTER(bNeedWarmUp)

	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta = (BlueprintThreadSafe))
	static bool GetNeedWarmUp(const FKawaiiPhysicsReference& KawaiiPhysics)
	KAWAIIPHYSICS_VALUE_GETTER(bool, bNeedWarmUp)

	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta = (BlueprintThreadSafe))
	static FKawaiiPhysicsReference SetLimitsDataAsset(const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                  UKawaiiPhysicsLimitsDataAsset* LimitsDataAsset);

	UFUNCTION(BlueprintPure, Category = "Kawaii Physics", meta = (BlueprintThreadSafe))
	static UKawaiiPhysicsLimitsDataAsset* GetLimitsDataAsset(const FKawaiiPhysicsReference& KawaiiPhysics);

	// --- external force stack ---

	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta = (BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FKawaiiPhysicsReference AddExternalForceWithExecResult(EKawaiiPhysicsAccessExternalForceResult& ExecResult,
	                                                              const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                              UPARAM(ref) FInstancedStruct& ExternalForce,
	                                                              UObject* Owner, int32 ChainIndex = 0);

	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta = (BlueprintThreadSafe))
	static bool AddExternalForce(const FKawaiiPhysicsReference& KawaiiPhysics,
	                             UPARAM(ref) FInstancedStruct& ExternalForce, UObject* Owner,
	                             bool bIsOneShot = false, int32 ChainIndex = 0);

	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta = (BlueprintThreadSafe))
	static bool AddExternalForcesToComponent(USkeletalMeshComponent* MeshComp,
	                                         UPARAM(ref) TArray<FInstancedStruct>& ExternalForces, UObject* Owner,
	                                         UPARAM(ref) FGameplayTagContainer& FilterTags,
	                                         bool bFilterExactMatch = false, bool bIsOneShot = false);

	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta = (BlueprintThreadSafe))
	static bool RemoveExternalForcesFromComponent(USkeletalMeshComponent* MeshComp, UObject* Owner,
	                                              UPARAM(ref) FGameplayTagContainer& FilterTags,
	                                              bool bFilterExactMatch = false);

	// --- typed access into an entry of a chain's ExternalForces ---

	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta = (BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FKawaiiPhysicsReference SetExternalForceBoolProperty(EKawaiiPhysicsAccessExternalForceResult& ExecResult,
	                                                            const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                            int ExternalForceIndex, FName PropertyName,
	                                                            bool Value, int32 ChainIndex = 0);

	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta = (BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static bool GetExternalForceBoolProperty(EKawaiiPhysicsAccessExternalForceResult& ExecResult,
	                                         const FKawaiiPhysicsReference& KawaiiPhysics, int ExternalForceIndex,
	                                         FName PropertyName, int32 ChainIndex = 0);

	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta = (BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FKawaiiPhysicsReference SetExternalForceIntProperty(EKawaiiPhysicsAccessExternalForceResult& ExecResult,
	                                                           const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                           int ExternalForceIndex, FName PropertyName,
	                                                           int32 Value, int32 ChainIndex = 0);

	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta = (BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static int32 GetExternalForceIntProperty(EKawaiiPhysicsAccessExternalForceResult& ExecResult,
	                                         const FKawaiiPhysicsReference& KawaiiPhysics, int ExternalForceIndex,
	                                         FName PropertyName, int32 ChainIndex = 0);

	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta = (BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FKawaiiPhysicsReference SetExternalForceFloatProperty(EKawaiiPhysicsAccessExternalForceResult& ExecResult,
	                                                             const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                             int ExternalForceIndex, FName PropertyName,
	                                                             float Value, int32 ChainIndex = 0);

	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta = (BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static float GetExternalForceFloatProperty(EKawaiiPhysicsAccessExternalForceResult& ExecResult,
	                                           const FKawaiiPhysicsReference& KawaiiPhysics, int ExternalForceIndex,
	                                           FName PropertyName, int32 ChainIndex = 0);

	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta = (BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FKawaiiPhysicsReference SetExternalForceVectorProperty(EKawaiiPhysicsAccessExternalForceResult& ExecResult,
	                                                              const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                              int ExternalForceIndex, FName PropertyName,
	                                                              FVector Value, int32 ChainIndex = 0);

	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta = (BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FVector GetExternalForceVectorProperty(EKawaiiPhysicsAccessExternalForceResult& ExecResult,
	                                              const FKawaiiPhysicsReference& KawaiiPhysics, int ExternalForceIndex,
	                                              FName PropertyName, int32 ChainIndex = 0);

	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta = (BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FKawaiiPhysicsReference SetExternalForceRotatorProperty(EKawaiiPhysicsAccessExternalForceResult& ExecResult,
	                                                               const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                               int ExternalForceIndex, FName PropertyName,
	                                                               FRotator Value, int32 ChainIndex = 0);

	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta = (BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FRotator GetExternalForceRotatorProperty(EKawaiiPhysicsAccessExternalForceResult& ExecResult,
	                                                const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                int ExternalForceIndex, FName PropertyName, int32 ChainIndex = 0);

	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta = (BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FKawaiiPhysicsReference SetExternalForceTransformProperty(
		EKawaiiPhysicsAccessExternalForceResult& ExecResult, const FKawaiiPhysicsReference& KawaiiPhysics,
		int ExternalForceIndex, FName PropertyName, FTransform Value, int32 ChainIndex = 0);

	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics",
		meta = (BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult"))
	static FTransform GetExternalForceTransformProperty(EKawaiiPhysicsAccessExternalForceResult& ExecResult,
	                                                    const FKawaiiPhysicsReference& KawaiiPhysics,
	                                                    int ExternalForceIndex, FName PropertyName,
	                                                    int32 ChainIndex = 0);

	/** Wildcard variants: the Value pin adopts whatever type is wired to it. */
	UFUNCTION(BlueprintCallable, CustomThunk, Category = "Kawaii Physics",
		meta = (BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult", CustomStructureParam = "Value"))
	static void SetExternalForceWildcardProperty(EKawaiiPhysicsAccessExternalForceResult& ExecResult,
	                                             const FKawaiiPhysicsReference& KawaiiPhysics, int ExternalForceIndex,
	                                             FName PropertyName, const int32& Value, int32 ChainIndex = 0);

	UFUNCTION(BlueprintCallable, CustomThunk, Category = "Kawaii Physics",
		meta = (BlueprintThreadSafe, ExpandEnumAsExecs = "ExecResult", CustomStructureParam = "Value"))
	static void GetExternalForceWildcardProperty(EKawaiiPhysicsAccessExternalForceResult& ExecResult,
	                                             const FKawaiiPhysicsReference& KawaiiPhysics, int ExternalForceIndex,
	                                             FName PropertyName, int32& Value, int32 ChainIndex = 0);

	DECLARE_FUNCTION(execSetExternalForceWildcardProperty);
	DECLARE_FUNCTION(execGetExternalForceWildcardProperty);

private:
	/** Shared body for the wildcard thunks. */
	static void SetExternalForceProperty_Internal(EKawaiiPhysicsAccessExternalForceResult& ExecResult,
	                                              const FKawaiiPhysicsReference& KawaiiPhysics, int ExternalForceIndex,
	                                              FName PropertyName, int32 ChainIndex,
	                                              const FProperty* SrcProperty, const void* SrcValue);

	static void GetExternalForceProperty_Internal(EKawaiiPhysicsAccessExternalForceResult& ExecResult,
	                                              const FKawaiiPhysicsReference& KawaiiPhysics, int ExternalForceIndex,
	                                              FName PropertyName, int32 ChainIndex,
	                                              const FProperty* DestProperty, void* DestValue);
};
