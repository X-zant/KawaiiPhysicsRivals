// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License

#pragma once
#include "AnimNode_KawaiiPhysics.h"
#include "Curves/CurveVector.h"
#include "KawaiiPhysicsCustomExternalForce.generated.h"

/**
 * Blueprint-authorable external force.
 *
 * The UFUNCTION signatures below are part of the game's reflected API -- a
 * cooked Blueprint that subclasses this binds to them by name and signature --
 * so they take the node rather than the chain, exactly as the game declares
 * them. The node exposes the chain currently being simulated for the duration
 * of the call; see FAnimNode_KawaiiPhysics::SimulateModifyBones.
 */
UCLASS(Abstract, Blueprintable, EditInlineNew, CollapseCategories)
class KAWAIIPHYSICS_API UKawaiiPhysics_CustomExternalForce : public UObject
{
	GENERATED_BODY()

public:
	/** [0] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayPriority = 1),
		Category = "KawaiiPhysics|CustomExternalForce")
	bool bIsEnabled = true;

	/** [1] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayPriority = 1),
		Category = "KawaiiPhysics|CustomExternalForce")
	bool bDrawDebug = false;

public:
	UFUNCTION(BlueprintNativeEvent)
	void PreApply(UPARAM(ref) FAnimNode_KawaiiPhysics& Node, const USkeletalMeshComponent* SkelComp);

	virtual void PreApply_Implementation(UPARAM(ref) FAnimNode_KawaiiPhysics& Node,
	                                     const USkeletalMeshComponent* SkelComp) PURE_VIRTUAL(,);

	UFUNCTION(BlueprintNativeEvent)
	void Apply(UPARAM(ref) FAnimNode_KawaiiPhysics& Node, int32 ModifyBoneIndex,
	           const USkeletalMeshComponent* SkelComp, const FTransform& BoneTransform);

	virtual void Apply_Implementation(UPARAM(ref) FAnimNode_KawaiiPhysics& Node, int32 ModifyBoneIndex,
	                                  const USkeletalMeshComponent* SkelComp, const FTransform& BoneTransform)
	{
	}

	UFUNCTION(BlueprintCallable, Category = "KawaiiPhysics|CustomExternalForce")
	virtual bool IsDebugEnabled()
	{
#if ENABLE_ANIM_DEBUG
		if (CVarAnimNodeKawaiiPhysicsDebug.GetValueOnAnyThread())
		{
			return bDrawDebug;
		}
#endif

		return false;
	}
};
