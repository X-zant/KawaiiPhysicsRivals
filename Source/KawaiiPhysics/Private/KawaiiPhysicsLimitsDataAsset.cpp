// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License

#include "KawaiiPhysicsLimitsDataAsset.h"

#if WITH_EDITORONLY_DATA
void UKawaiiPhysicsLimitsDataAsset::Serialize(FStructuredArchiveRecord Record)
{
	Super::Serialize(Record);
}
#endif

void UKawaiiPhysicsLimitsDataAsset::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITORONLY_DATA
	// Fold the pre-refactor arrays into the live ones, once.
	auto Convert = [](const auto& Source, auto& Dest)
	{
		for (const auto& Entry : Source)
		{
			Dest.Add(Entry.Convert());
		}
	};

	if (!SphericalLimitsData.IsEmpty() && SphericalLimits.IsEmpty()) Convert(SphericalLimitsData, SphericalLimits);
	if (!CapsuleLimitsData.IsEmpty() && CapsuleLimits.IsEmpty()) Convert(CapsuleLimitsData, CapsuleLimits);
	if (!BoxLimitsData.IsEmpty() && BoxLimits.IsEmpty()) Convert(BoxLimitsData, BoxLimits);
	if (!PlanarLimitsData.IsEmpty() && PlanarLimits.IsEmpty()) Convert(PlanarLimitsData, PlanarLimits);

	SphericalLimitsData.Empty();
	CapsuleLimitsData.Empty();
	BoxLimitsData.Empty();
	PlanarLimitsData.Empty();

	Sync();
#endif
}

USkeleton* UKawaiiPhysicsLimitsDataAsset::GetSkeleton(bool& bInvalidSkeletonIsError,
                                                      const IPropertyHandle* PropertyHandle)
{
	bInvalidSkeletonIsError = false;

#if WITH_EDITORONLY_DATA
	return Skeleton;
#else
	return nullptr;
#endif
}

#if WITH_EDITOR

void UKawaiiPhysicsLimitsDataAsset::UpdateLimit(FCollisionLimitBase* Limit)
{
	if (!Limit)
	{
		return;
	}

	Limit->SourceType = ECollisionSourceType::DataAsset;
	Sync();
}

void UKawaiiPhysicsLimitsDataAsset::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);

	Sync();
	OnLimitsChanged.Broadcast(PropertyChangedEvent);
}

void UKawaiiPhysicsLimitsDataAsset::Sync()
{
	// Every limit that lives in this asset is, by definition, DataAsset-sourced.
	// Nodes read SourceType to decide what they are allowed to edit in-place.
	auto Tag = [](auto& Limits, ECollisionLimitType Type)
	{
		for (auto& Limit : Limits)
		{
			Limit.SourceType = ECollisionSourceType::DataAsset;
#if WITH_EDITORONLY_DATA
			Limit.Type = Type;
			if (!Limit.Guid.IsValid())
			{
				Limit.Guid = FGuid::NewGuid();
			}
#endif
		}
	};

	Tag(SphericalLimits, ECollisionLimitType::Spherical);
	Tag(CapsuleLimits, ECollisionLimitType::Capsule);
	Tag(BoxLimits, ECollisionLimitType::Box);
	Tag(PlanarLimits, ECollisionLimitType::Planar);
}

#endif
