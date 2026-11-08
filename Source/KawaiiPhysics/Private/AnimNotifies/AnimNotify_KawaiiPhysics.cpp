// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License

#include "AnimNotifies/AnimNotify_KawaiiPhysics.h"

#include "KawaiiPhysicsExternalForce.h"
#include "KawaiiPhysicsLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequenceBase.h"
#include "Logging/MessageLog.h"
#include "Misc/UObjectToken.h"

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

	// As upstream: an AdditionalExternalForces entry with no type set does
	// nothing at runtime, and is easy to leave behind by accident -- say so.
	static const FName NAME_AssetCheck("AssetCheck");

	const UAnimSequenceBase* ContainingAsset = Cast<UAnimSequenceBase>(GetContainingAsset());
	if (!ContainingAsset)
	{
		return;
	}

	for (const FInstancedStruct& Force : AdditionalExternalForces)
	{
		if (Force.IsValid())
		{
			continue;
		}

		const FText Message = FText::Format(
			NSLOCTEXT("AnimNotify", "ExternalForce_ShouldSet",
			          " AnimNotify(KawaiiPhysics_AddExternalForce) has an AdditionalExternalForces entry with no force type set, in {0}"),
			FText::AsCultureInvariant(ContainingAsset->GetPathName()));

		FMessageLog AssetCheckLog(NAME_AssetCheck);
		AssetCheckLog.Warning()
		             ->AddToken(FUObjectToken::Create(ContainingAsset))
		             ->AddToken(FTextToken::Create(Message));

		if (GIsEditor)
		{
			constexpr bool bForce = true;
			AssetCheckLog.Notify(Message, EMessageSeverity::Warning, bForce);
		}
	}
}
#endif
