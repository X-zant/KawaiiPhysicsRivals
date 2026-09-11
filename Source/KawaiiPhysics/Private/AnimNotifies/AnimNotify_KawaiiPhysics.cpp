// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License

#include "AnimNotifies/AnimNotify_KawaiiPhysics.h"

#include "KawaiiPhysicsExternalForce.h"
#include "KawaiiPhysicsLibrary.h"
#include "Components/SkeletalMeshComponent.h"

UAnimNotify_KawaiiPhysicsAddExternalForce::UAnimNotify_KawaiiPhysicsAddExternalForce(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	NotifyColor = FColor(200, 120, 200);
#endif
}

FString UAnimNotify_KawaiiPhysicsAddExternalForce::GetNotifyName_Implementation() const
{
	return TEXT("KawaiiPhysics: Add ExternalForce");
}

void UAnimNotify_KawaiiPhysicsAddExternalForce::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
                                                       const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!MeshComp || AdditionalExternalForces.IsEmpty())
	{
		return;
	}

	// One-shot: the force applies on the frame it lands and removes itself, so
	// there is no matching NotifyEnd to clean up after it.
	UKawaiiPhysicsLibrary::AddExternalForcesToComponent(MeshComp, AdditionalExternalForces, this, FilterTags,
	                                                    bFilterExactMatch, true);
}

#if WITH_EDITOR
void UAnimNotify_KawaiiPhysicsAddExternalForce::ValidateAssociatedAssets()
{
	Super::ValidateAssociatedAssets();
}
#endif
