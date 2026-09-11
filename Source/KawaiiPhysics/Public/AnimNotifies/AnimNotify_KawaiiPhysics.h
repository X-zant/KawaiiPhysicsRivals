// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Animation/AnimNotifies/AnimNotify.h"

#if	ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 5
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "AnimNotify_KawaiiPhysics.generated.h"

/**
 * Pushes one-shot external forces into every KawaiiPhysics node on the mesh
 * whose KawaiiPhysicsTag passes FilterTags.
 */
UCLASS(Blueprintable, meta = (DisplayName = "KawaiiPhyiscs: Add ExternalForce"))
class KAWAIIPHYSICS_API UAnimNotify_KawaiiPhysicsAddExternalForce : public UAnimNotify
{
	GENERATED_BODY()

public:
	UAnimNotify_KawaiiPhysicsAddExternalForce(const FObjectInitializer& ObjectInitializer);

	virtual FString GetNotifyName_Implementation() const override;

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	                    const FAnimNotifyEventReference& EventReference) override;

public:
	/** [0] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ExternalForce",
		meta = (BaseStruct = "/Script/KawaiiPhysics.KawaiiPhysics_ExternalForce", ExcludeBaseStruct))
	TArray<FInstancedStruct> AdditionalExternalForces;

	/** [1] Empty matches every node. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ExternalForce")
	FGameplayTagContainer FilterTags;

	/** [2] When false, parent tags match too. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ExternalForce")
	bool bFilterExactMatch = false;

#if WITH_EDITOR
	virtual void ValidateAssociatedAssets() override;
#endif
};
