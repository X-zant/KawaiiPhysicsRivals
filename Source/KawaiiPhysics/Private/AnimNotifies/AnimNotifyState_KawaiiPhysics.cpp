// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License

#include "AnimNotifies/AnimNotifyState_KawaiiPhysics.h"

#include "KawaiiPhysicsExternalForce.h"
#include "KawaiiPhysicsLibrary.h"
#include "Components/SkeletalMeshComponent.h"

UAnimNotifyState_KawaiiPhysicsAddExternalForce::UAnimNotifyState_KawaiiPhysicsAddExternalForce(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	NotifyColor = FColor(200, 120, 200);
#endif
}

FString UAnimNotifyState_KawaiiPhysicsAddExternalForce::GetNotifyName_Implementation() const
{
	return TEXT("KawaiiPhysics: Add ExternalForce");
}

void UAnimNotifyState_KawaiiPhysicsAddExternalForce::NotifyBegin(USkeletalMeshComponent* MeshComp,
                                                                 UAnimSequenceBase* Animation, float TotalDuration,
                                                                 const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	if (!MeshComp || AdditionalExternalForces.IsEmpty())
	{
		return;
	}

	// Not one-shot: these persist for the notify window and are removed in
	// NotifyEnd, matched by owner.
	UKawaiiPhysicsLibrary::AddExternalForcesToComponent(MeshComp, AdditionalExternalForces, this, FilterTags,
	                                                    bFilterExactMatch, false);
}

void UAnimNotifyState_KawaiiPhysicsAddExternalForce::NotifyEnd(USkeletalMeshComponent* MeshComp,
                                                               UAnimSequenceBase* Animation,
                                                               const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	if (!MeshComp)
	{
		return;
	}

	UKawaiiPhysicsLibrary::RemoveExternalForcesFromComponent(MeshComp, this, FilterTags, bFilterExactMatch);
}

#if WITH_EDITOR
void UAnimNotifyState_KawaiiPhysicsAddExternalForce::ValidateAssociatedAssets()
{
	Super::ValidateAssociatedAssets();
}
#endif
