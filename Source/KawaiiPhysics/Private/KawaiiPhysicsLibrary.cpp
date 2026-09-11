// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License

#include "KawaiiPhysicsLibrary.h"

#include "KawaiiPhysics.h"
#include "KawaiiPhysicsLimitsDataAsset.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"

namespace
{
	/**
	 * Locates one external force inside a chain and hands it to the visitor.
	 *
	 * Returns false when the node, chain or force index does not resolve, which
	 * is what the ExecResult pins report back to Blueprint.
	 */
	template <typename VisitorType>
	bool VisitExternalForce(const FKawaiiPhysicsReference& KawaiiPhysics, int32 ChainIndex, int32 ExternalForceIndex,
	                        VisitorType&& Visitor)
	{
		bool bFound = false;

		KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
			TEXT("VisitExternalForce"),
			[&](FAnimNode_KawaiiPhysics& Node)
			{
				FKawaiiPhysicsChain* Chain = Node.GetChain(ChainIndex);
				if (!Chain || !Chain->ExternalForceSettings.ExternalForces.IsValidIndex(ExternalForceIndex))
				{
					return;
				}

				FInstancedStruct& ForceStruct = Chain->ExternalForceSettings.ExternalForces[ExternalForceIndex];
				if (!ForceStruct.IsValid())
				{
					return;
				}

				bFound = true;
				Visitor(ForceStruct);
			});

		return bFound;
	}

	/** Finds PropertyName on the force's actual (derived) struct type. */
	FProperty* FindForceProperty(const FInstancedStruct& ForceStruct, FName PropertyName)
	{
		const UScriptStruct* Type = ForceStruct.GetScriptStruct();
		return Type ? Type->FindPropertyByName(PropertyName) : nullptr;
	}
}

FKawaiiPhysicsReference UKawaiiPhysicsLibrary::ConvertToKawaiiPhysics(const FAnimNodeReference& Node,
                                                                      EAnimNodeReferenceConversionResult& Result)
{
	return FAnimNodeReference::ConvertToType<FKawaiiPhysicsReference>(Node, Result);
}

bool UKawaiiPhysicsLibrary::CollectKawaiiPhysicsNodes(TArray<FKawaiiPhysicsReference>& Nodes,
                                                      UAnimInstance* AnimInstance,
                                                      const FGameplayTagContainer& FilterTags,
                                                      bool bFilterExactMatch)
{
	if (!AnimInstance)
	{
		return false;
	}

	IAnimClassInterface* AnimClass = IAnimClassInterface::GetFromClass(AnimInstance->GetClass());
	if (!AnimClass)
	{
		return false;
	}

	const int32 CountBefore = Nodes.Num();

	for (const FStructProperty* NodeProperty : AnimClass->GetAnimNodeProperties())
	{
		if (!NodeProperty || !NodeProperty->Struct->IsChildOf(FAnimNode_KawaiiPhysics::StaticStruct()))
		{
			continue;
		}

		FAnimNode_KawaiiPhysics* KawaiiNode =
			NodeProperty->ContainerPtrToValuePtr<FAnimNode_KawaiiPhysics>(AnimInstance);
		if (!KawaiiNode)
		{
			continue;
		}

		// An empty filter means "every node"; otherwise the node's own tag must
		// be in the container, exactly or by parentage.
		if (!FilterTags.IsEmpty())
		{
			const bool bMatches = bFilterExactMatch
				                      ? FilterTags.HasTagExact(KawaiiNode->KawaiiPhysicsTag)
				                      : FilterTags.HasTag(KawaiiNode->KawaiiPhysicsTag);
			if (!bMatches)
			{
				continue;
			}
		}

		// FKawaiiPhysicsReference adds no members over FAnimNodeReference, so
		// slicing the base in is the whole conversion.
		FKawaiiPhysicsReference Reference;
		static_cast<FAnimNodeReference&>(Reference) = FAnimNodeReference(AnimInstance, *KawaiiNode);
		Nodes.Add(Reference);
	}

	return Nodes.Num() > CountBefore;
}

bool UKawaiiPhysicsLibrary::CollectKawaiiPhysicsNodes(TArray<FKawaiiPhysicsReference>& Nodes,
                                                      USkeletalMeshComponent* MeshComp,
                                                      const FGameplayTagContainer& FilterTags,
                                                      bool bFilterExactMatch)
{
	if (!MeshComp)
	{
		return false;
	}

	bool bFound = false;

	// Walks the main instance, any linked layers, and the post-process instance.
	// Linked layers carry their own node instances, so they have to be included.
	// (GetLinkedAnimInstances() is not usable here: the public overload is const,
	// so a non-const component selects the private one.)
	MeshComp->ForEachAnimInstance([&](UAnimInstance* AnimInstance)
	{
		bFound |= CollectKawaiiPhysicsNodes(Nodes, AnimInstance, FilterTags, bFilterExactMatch);
	});

	return bFound;
}

FKawaiiPhysicsReference UKawaiiPhysicsLibrary::ResetDynamics(const FKawaiiPhysicsReference& KawaiiPhysics)
{
	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("ResetDynamics"),
		[](FAnimNode_KawaiiPhysics& Node) { Node.ResetDynamics(ETeleportType::ResetPhysics); });

	return KawaiiPhysics;
}

FKawaiiPhysicsReference UKawaiiPhysicsLibrary::SetRootBoneName(const FKawaiiPhysicsReference& KawaiiPhysics,
                                                               FName& RootBoneName, int32 ChainIndex)
{
	const FName LocalName = RootBoneName;
	const int32 LocalChainIndex = ChainIndex;

	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("SetRootBoneName"),
		[LocalName, LocalChainIndex](FAnimNode_KawaiiPhysics& Node)
		{
			if (FKawaiiPhysicsChain* Chain = Node.GetChain(LocalChainIndex))
			{
				Chain->BoneSettings.RootBone.BoneName = LocalName;
				// The bone set changed, so the chain has to be rebuilt.
				Chain->bInitialized = false;
			}
		});

	return KawaiiPhysics;
}

FName UKawaiiPhysicsLibrary::GetRootBoneName(const FKawaiiPhysicsReference& KawaiiPhysics, int32 ChainIndex)
KAWAIIPHYSICS_CHAIN_GETTER(FName, RootBoneName, BoneSettings.RootBone.BoneName)

FKawaiiPhysicsReference UKawaiiPhysicsLibrary::SetExcludeBoneNames(const FKawaiiPhysicsReference& KawaiiPhysics,
                                                                   TArray<FName>& ExcludeBoneNames, int32 ChainIndex)
{
	const TArray<FName> LocalNames = ExcludeBoneNames;
	const int32 LocalChainIndex = ChainIndex;

	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("SetExcludeBoneNames"),
		[LocalNames, LocalChainIndex](FAnimNode_KawaiiPhysics& Node)
		{
			if (FKawaiiPhysicsChain* Chain = Node.GetChain(LocalChainIndex))
			{
				Chain->BoneSettings.ExcludeBones.Reset();
				for (const FName& Name : LocalNames)
				{
					FBoneReference Bone;
					Bone.BoneName = Name;
					Chain->BoneSettings.ExcludeBones.Add(Bone);
				}
				Chain->bInitialized = false;
			}
		});

	return KawaiiPhysics;
}

TArray<FName> UKawaiiPhysicsLibrary::GetExcludeBoneNames(const FKawaiiPhysicsReference& KawaiiPhysics,
                                                         int32 ChainIndex)
{
	TArray<FName> Names;
	const int32 LocalChainIndex = ChainIndex;

	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("GetExcludeBoneNames"),
		[&Names, LocalChainIndex](FAnimNode_KawaiiPhysics& Node)
		{
			if (const FKawaiiPhysicsChain* Chain = Node.GetChain(LocalChainIndex))
			{
				for (const FBoneReference& Bone : Chain->BoneSettings.ExcludeBones)
				{
					Names.Add(Bone.BoneName);
				}
			}
		});

	return Names;
}

FKawaiiPhysicsReference UKawaiiPhysicsLibrary::SetLimitsDataAsset(const FKawaiiPhysicsReference& KawaiiPhysics,
                                                                  UKawaiiPhysicsLimitsDataAsset* LimitsDataAsset)
{
	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("SetLimitsDataAsset"),
		[LimitsDataAsset](FAnimNode_KawaiiPhysics& Node) { Node.LimitsDataAsset = LimitsDataAsset; });

	return KawaiiPhysics;
}

UKawaiiPhysicsLimitsDataAsset* UKawaiiPhysicsLibrary::GetLimitsDataAsset(const FKawaiiPhysicsReference& KawaiiPhysics)
{
	UKawaiiPhysicsLimitsDataAsset* Value = nullptr;

	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("GetLimitsDataAsset"),
		[&Value](FAnimNode_KawaiiPhysics& Node) { Value = Node.LimitsDataAsset; });

	return Value;
}

///////////////////////////////////////////////////////////////////////////////
// External force stack
///////////////////////////////////////////////////////////////////////////////

bool UKawaiiPhysicsLibrary::AddExternalForce(const FKawaiiPhysicsReference& KawaiiPhysics,
                                             FInstancedStruct& ExternalForce, UObject* Owner, bool bIsOneShot,
                                             int32 ChainIndex)
{
	if (!ExternalForce.IsValid())
	{
		return false;
	}

	FInstancedStruct LocalForce = ExternalForce;
	const int32 LocalChainIndex = ChainIndex;
	bool bAdded = false;

	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("AddExternalForce"),
		[&bAdded, LocalForce, Owner, bIsOneShot, LocalChainIndex](FAnimNode_KawaiiPhysics& Node)
		{
			FKawaiiPhysicsChain* Chain = Node.GetChain(LocalChainIndex);
			if (!Chain)
			{
				return;
			}

			const int32 Index = Chain->ExternalForceSettings.ExternalForces.Add(LocalForce);
			if (auto* Force = Chain->ExternalForceSettings.ExternalForces[Index]
			                       .GetMutablePtr<FKawaiiPhysics_ExternalForce>())
			{
				// Owner is what RemoveExternalForcesFromComponent matches on later.
				Force->ExternalOwner = Owner;
				Force->bIsOneShot = bIsOneShot;
			}

			bAdded = true;
		});

	return bAdded;
}

FKawaiiPhysicsReference UKawaiiPhysicsLibrary::AddExternalForceWithExecResult(
	EKawaiiPhysicsAccessExternalForceResult& ExecResult, const FKawaiiPhysicsReference& KawaiiPhysics,
	FInstancedStruct& ExternalForce, UObject* Owner, int32 ChainIndex)
{
	ExecResult = AddExternalForce(KawaiiPhysics, ExternalForce, Owner, false, ChainIndex)
		             ? EKawaiiPhysicsAccessExternalForceResult::Valid
		             : EKawaiiPhysicsAccessExternalForceResult::NotValid;

	return KawaiiPhysics;
}

bool UKawaiiPhysicsLibrary::AddExternalForcesToComponent(USkeletalMeshComponent* MeshComp,
                                                         TArray<FInstancedStruct>& ExternalForces, UObject* Owner,
                                                         FGameplayTagContainer& FilterTags, bool bFilterExactMatch,
                                                         bool bIsOneShot)
{
	TArray<FKawaiiPhysicsReference> Nodes;
	if (!CollectKawaiiPhysicsNodes(Nodes, MeshComp, FilterTags, bFilterExactMatch))
	{
		return false;
	}

	bool bAddedAny = false;
	for (const FKawaiiPhysicsReference& Node : Nodes)
	{
		for (FInstancedStruct& Force : ExternalForces)
		{
			// Applied to every chain: the caller filtered by node tag, not by chain.
			int32 ChainCount = 0;
			Node.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
				TEXT("GetChainCount"),
				[&ChainCount](FAnimNode_KawaiiPhysics& KawaiiNode) { ChainCount = KawaiiNode.Chains.Num(); });

			for (int32 ChainIndex = 0; ChainIndex < ChainCount; ++ChainIndex)
			{
				bAddedAny |= AddExternalForce(Node, Force, Owner, bIsOneShot, ChainIndex);
			}
		}
	}

	return bAddedAny;
}

bool UKawaiiPhysicsLibrary::RemoveExternalForcesFromComponent(USkeletalMeshComponent* MeshComp, UObject* Owner,
                                                              FGameplayTagContainer& FilterTags,
                                                              bool bFilterExactMatch)
{
	TArray<FKawaiiPhysicsReference> Nodes;
	if (!CollectKawaiiPhysicsNodes(Nodes, MeshComp, FilterTags, bFilterExactMatch))
	{
		return false;
	}

	bool bRemovedAny = false;
	for (const FKawaiiPhysicsReference& Node : Nodes)
	{
		Node.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
			TEXT("RemoveExternalForces"),
			[&bRemovedAny, Owner](FAnimNode_KawaiiPhysics& KawaiiNode)
			{
				for (FKawaiiPhysicsChain& Chain : KawaiiNode.Chains)
				{
					const int32 Removed = Chain.ExternalForceSettings.ExternalForces.RemoveAll(
						[Owner](const FInstancedStruct& ForceStruct)
						{
							const auto* Force = ForceStruct.GetPtr<FKawaiiPhysics_ExternalForce>();
							return Force && Force->ExternalOwner == Owner;
						});

					bRemovedAny |= Removed > 0;
				}
			});
	}

	return bRemovedAny;
}

///////////////////////////////////////////////////////////////////////////////
// Typed property access into a force
///////////////////////////////////////////////////////////////////////////////

// Set/Get pairs for each concrete type all reduce to "find the FProperty on the
// force's real struct type, check it is the type the caller asked for, copy".
#define KAWAIIPHYSICS_DEFINE_FORCE_SETTER(FuncSuffix, ValueType, PropertyClass) \
	FKawaiiPhysicsReference UKawaiiPhysicsLibrary::SetExternalForce##FuncSuffix##Property( \
		EKawaiiPhysicsAccessExternalForceResult& ExecResult, const FKawaiiPhysicsReference& KawaiiPhysics, \
		int ExternalForceIndex, FName PropertyName, ValueType Value, int32 ChainIndex) \
	{ \
		bool bOk = false; \
		VisitExternalForce(KawaiiPhysics, ChainIndex, ExternalForceIndex, \
			[&](FInstancedStruct& ForceStruct) \
			{ \
				if (const PropertyClass* Property = CastField<PropertyClass>( \
					FindForceProperty(ForceStruct, PropertyName))) \
				{ \
					Property->SetPropertyValue_InContainer(ForceStruct.GetMutableMemory(), Value); \
					bOk = true; \
				} \
			}); \
		ExecResult = bOk ? EKawaiiPhysicsAccessExternalForceResult::Valid \
						 : EKawaiiPhysicsAccessExternalForceResult::NotValid; \
		return KawaiiPhysics; \
	}

#define KAWAIIPHYSICS_DEFINE_FORCE_GETTER(FuncSuffix, ValueType, PropertyClass) \
	ValueType UKawaiiPhysicsLibrary::GetExternalForce##FuncSuffix##Property( \
		EKawaiiPhysicsAccessExternalForceResult& ExecResult, const FKawaiiPhysicsReference& KawaiiPhysics, \
		int ExternalForceIndex, FName PropertyName, int32 ChainIndex) \
	{ \
		ValueType Value = ValueType(); \
		bool bOk = false; \
		VisitExternalForce(KawaiiPhysics, ChainIndex, ExternalForceIndex, \
			[&](FInstancedStruct& ForceStruct) \
			{ \
				if (const PropertyClass* Property = CastField<PropertyClass>( \
					FindForceProperty(ForceStruct, PropertyName))) \
				{ \
					Value = Property->GetPropertyValue_InContainer(ForceStruct.GetMemory()); \
					bOk = true; \
				} \
			}); \
		ExecResult = bOk ? EKawaiiPhysicsAccessExternalForceResult::Valid \
						 : EKawaiiPhysicsAccessExternalForceResult::NotValid; \
		return Value; \
	}

KAWAIIPHYSICS_DEFINE_FORCE_SETTER(Bool, bool, FBoolProperty)
KAWAIIPHYSICS_DEFINE_FORCE_GETTER(Bool, bool, FBoolProperty)
KAWAIIPHYSICS_DEFINE_FORCE_SETTER(Int, int32, FIntProperty)
KAWAIIPHYSICS_DEFINE_FORCE_GETTER(Int, int32, FIntProperty)
KAWAIIPHYSICS_DEFINE_FORCE_SETTER(Float, float, FFloatProperty)
KAWAIIPHYSICS_DEFINE_FORCE_GETTER(Float, float, FFloatProperty)

#undef KAWAIIPHYSICS_DEFINE_FORCE_SETTER
#undef KAWAIIPHYSICS_DEFINE_FORCE_GETTER

// Struct-valued properties go through the struct property rather than a typed
// accessor, so they get their own pair.
#define KAWAIIPHYSICS_DEFINE_FORCE_STRUCT_ACCESSORS(FuncSuffix, ValueType, StructName) \
	FKawaiiPhysicsReference UKawaiiPhysicsLibrary::SetExternalForce##FuncSuffix##Property( \
		EKawaiiPhysicsAccessExternalForceResult& ExecResult, const FKawaiiPhysicsReference& KawaiiPhysics, \
		int ExternalForceIndex, FName PropertyName, ValueType Value, int32 ChainIndex) \
	{ \
		bool bOk = false; \
		VisitExternalForce(KawaiiPhysics, ChainIndex, ExternalForceIndex, \
			[&](FInstancedStruct& ForceStruct) \
			{ \
				const FStructProperty* Property = CastField<FStructProperty>( \
					FindForceProperty(ForceStruct, PropertyName)); \
				if (Property && Property->Struct == TBaseStructure<ValueType>::Get()) \
				{ \
					*Property->ContainerPtrToValuePtr<ValueType>(ForceStruct.GetMutableMemory()) = Value; \
					bOk = true; \
				} \
			}); \
		ExecResult = bOk ? EKawaiiPhysicsAccessExternalForceResult::Valid \
						 : EKawaiiPhysicsAccessExternalForceResult::NotValid; \
		return KawaiiPhysics; \
	} \
	ValueType UKawaiiPhysicsLibrary::GetExternalForce##FuncSuffix##Property( \
		EKawaiiPhysicsAccessExternalForceResult& ExecResult, const FKawaiiPhysicsReference& KawaiiPhysics, \
		int ExternalForceIndex, FName PropertyName, int32 ChainIndex) \
	{ \
		ValueType Value = ValueType(); \
		bool bOk = false; \
		VisitExternalForce(KawaiiPhysics, ChainIndex, ExternalForceIndex, \
			[&](FInstancedStruct& ForceStruct) \
			{ \
				const FStructProperty* Property = CastField<FStructProperty>( \
					FindForceProperty(ForceStruct, PropertyName)); \
				if (Property && Property->Struct == TBaseStructure<ValueType>::Get()) \
				{ \
					Value = *Property->ContainerPtrToValuePtr<ValueType>(ForceStruct.GetMemory()); \
					bOk = true; \
				} \
			}); \
		ExecResult = bOk ? EKawaiiPhysicsAccessExternalForceResult::Valid \
						 : EKawaiiPhysicsAccessExternalForceResult::NotValid; \
		return Value; \
	}

KAWAIIPHYSICS_DEFINE_FORCE_STRUCT_ACCESSORS(Vector, FVector, Vector)
KAWAIIPHYSICS_DEFINE_FORCE_STRUCT_ACCESSORS(Rotator, FRotator, Rotator)
KAWAIIPHYSICS_DEFINE_FORCE_STRUCT_ACCESSORS(Transform, FTransform, Transform)

#undef KAWAIIPHYSICS_DEFINE_FORCE_STRUCT_ACCESSORS

///////////////////////////////////////////////////////////////////////////////
// Wildcard access
///////////////////////////////////////////////////////////////////////////////

void UKawaiiPhysicsLibrary::SetExternalForceWildcardProperty(EKawaiiPhysicsAccessExternalForceResult& ExecResult,
                                                             const FKawaiiPhysicsReference& KawaiiPhysics,
                                                             int ExternalForceIndex, FName PropertyName,
                                                             const int32& Value, int32 ChainIndex)
{
	// Never called directly: Blueprint always routes through the custom thunk,
	// which is what knows the real type behind the wildcard pin.
	checkNoEntry();
}

void UKawaiiPhysicsLibrary::GetExternalForceWildcardProperty(EKawaiiPhysicsAccessExternalForceResult& ExecResult,
                                                             const FKawaiiPhysicsReference& KawaiiPhysics,
                                                             int ExternalForceIndex, FName PropertyName,
                                                             int32& Value, int32 ChainIndex)
{
	checkNoEntry();
}

void UKawaiiPhysicsLibrary::SetExternalForceProperty_Internal(EKawaiiPhysicsAccessExternalForceResult& ExecResult,
                                                              const FKawaiiPhysicsReference& KawaiiPhysics,
                                                              int ExternalForceIndex, FName PropertyName,
                                                              int32 ChainIndex, const FProperty* SrcProperty,
                                                              const void* SrcValue)
{
	bool bOk = false;

	if (SrcProperty && SrcValue)
	{
		VisitExternalForce(KawaiiPhysics, ChainIndex, ExternalForceIndex,
		                   [&](FInstancedStruct& ForceStruct)
		                   {
			                   FProperty* DestProperty = FindForceProperty(ForceStruct, PropertyName);
			                   if (DestProperty && DestProperty->SameType(SrcProperty))
			                   {
				                   DestProperty->CopyCompleteValue(
					                   DestProperty->ContainerPtrToValuePtr<void>(ForceStruct.GetMutableMemory()),
					                   SrcValue);
				                   bOk = true;
			                   }
		                   });
	}

	ExecResult = bOk
		             ? EKawaiiPhysicsAccessExternalForceResult::Valid
		             : EKawaiiPhysicsAccessExternalForceResult::NotValid;
}

void UKawaiiPhysicsLibrary::GetExternalForceProperty_Internal(EKawaiiPhysicsAccessExternalForceResult& ExecResult,
                                                              const FKawaiiPhysicsReference& KawaiiPhysics,
                                                              int ExternalForceIndex, FName PropertyName,
                                                              int32 ChainIndex, const FProperty* DestProperty,
                                                              void* DestValue)
{
	bool bOk = false;

	if (DestProperty && DestValue)
	{
		VisitExternalForce(KawaiiPhysics, ChainIndex, ExternalForceIndex,
		                   [&](FInstancedStruct& ForceStruct)
		                   {
			                   const FProperty* SrcProperty = FindForceProperty(ForceStruct, PropertyName);
			                   if (SrcProperty && SrcProperty->SameType(DestProperty))
			                   {
				                   SrcProperty->CopyCompleteValue(
					                   DestValue,
					                   SrcProperty->ContainerPtrToValuePtr<void>(ForceStruct.GetMemory()));
				                   bOk = true;
			                   }
		                   });
	}

	ExecResult = bOk
		             ? EKawaiiPhysicsAccessExternalForceResult::Valid
		             : EKawaiiPhysicsAccessExternalForceResult::NotValid;
}

DEFINE_FUNCTION(UKawaiiPhysicsLibrary::execSetExternalForceWildcardProperty)
{
	P_GET_ENUM_REF(EKawaiiPhysicsAccessExternalForceResult, ExecResult);
	P_GET_STRUCT_REF(FKawaiiPhysicsReference, KawaiiPhysics);
	P_GET_PROPERTY(FIntProperty, ExternalForceIndex);
	P_GET_PROPERTY(FNameProperty, PropertyName);

	// The wildcard pin: read whatever property the graph actually wired up.
	Stack.MostRecentPropertyAddress = nullptr;
	Stack.MostRecentProperty = nullptr;
	Stack.StepCompiledIn<FProperty>(nullptr);
	const FProperty* SrcProperty = Stack.MostRecentProperty;
	const void* SrcValue = Stack.MostRecentPropertyAddress;

	P_GET_PROPERTY(FIntProperty, ChainIndex);

	P_FINISH;

	P_NATIVE_BEGIN;
		SetExternalForceProperty_Internal(ExecResult, KawaiiPhysics, ExternalForceIndex, PropertyName, ChainIndex,
		                                  SrcProperty, SrcValue);
	P_NATIVE_END;
}

DEFINE_FUNCTION(UKawaiiPhysicsLibrary::execGetExternalForceWildcardProperty)
{
	P_GET_ENUM_REF(EKawaiiPhysicsAccessExternalForceResult, ExecResult);
	P_GET_STRUCT_REF(FKawaiiPhysicsReference, KawaiiPhysics);
	P_GET_PROPERTY(FIntProperty, ExternalForceIndex);
	P_GET_PROPERTY(FNameProperty, PropertyName);

	Stack.MostRecentPropertyAddress = nullptr;
	Stack.MostRecentProperty = nullptr;
	Stack.StepCompiledIn<FProperty>(nullptr);
	const FProperty* DestProperty = Stack.MostRecentProperty;
	void* DestValue = Stack.MostRecentPropertyAddress;

	P_GET_PROPERTY(FIntProperty, ChainIndex);

	P_FINISH;

	P_NATIVE_BEGIN;
		GetExternalForceProperty_Internal(ExecResult, KawaiiPhysics, ExternalForceIndex, PropertyName, ChainIndex,
		                                  DestProperty, DestValue);
	P_NATIVE_END;
}
