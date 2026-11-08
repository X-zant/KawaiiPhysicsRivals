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
	auto Convert = [](auto& Source, auto& Dest)
	{
		for (auto& Entry : Source)
		{
			// The oldest format only stored DrivingBoneName. Upstream migrates it
			// by custom version; this plugin does not register upstream's
			// versions (an unknown NEWER one would refuse to load), so detect it
			// from the data instead.
			if (Entry.DrivingBoneReference.BoneName == NAME_None && Entry.DrivingBoneName != NAME_None)
			{
				Entry.DrivingBoneReference = FBoneReference(Entry.DrivingBoneName);
			}
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

namespace
{
	/** Overwrites the entry with the same Guid -- the identity an edit is matched on. */
	template <typename LimitType>
	void UpdateCollisionLimit(TArray<LimitType>& Limits, const LimitType& NewLimit)
	{
		for (LimitType& Existing : Limits)
		{
			if (Existing.Guid == NewLimit.Guid)
			{
				Existing = NewLimit;
				break;
			}
		}
	}
}

void UKawaiiPhysicsLimitsDataAsset::UpdateLimit(FCollisionLimitBase* Limit)
{
	if (!Limit)
	{
		return;
	}

	// As upstream: write the edited limit back over the asset's own entry, so a
	// viewport edit is actually stored in the asset.
	Limit->SourceType = ECollisionSourceType::DataAsset;
	switch (Limit->Type)
	{
	case ECollisionLimitType::Spherical:
		UpdateCollisionLimit(SphericalLimits, *static_cast<FSphericalLimit*>(Limit));
		break;
	case ECollisionLimitType::Capsule:
		UpdateCollisionLimit(CapsuleLimits, *static_cast<FCapsuleLimit*>(Limit));
		break;
	case ECollisionLimitType::Box:
		UpdateCollisionLimit(BoxLimits, *static_cast<FBoxLimit*>(Limit));
		break;
	case ECollisionLimitType::Planar:
		UpdateCollisionLimit(PlanarLimits, *static_cast<FPlanarLimit*>(Limit));
		break;
	default:
		break;
	}

	MarkPackageDirty();
}

void UKawaiiPhysicsLimitsDataAsset::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);

	// A duplicated array entry copies its Guid; give it its own, as upstream
	// does, or edits to one would be applied to the other.
	if (PropertyChangedEvent.ChangeType == EPropertyChangeType::Duplicate && PropertyChangedEvent.MemberProperty)
	{
		const FName ArrayName = PropertyChangedEvent.MemberProperty->GetFName();
		const int32 Index = PropertyChangedEvent.GetArrayIndex(ArrayName.ToString());
		auto Regenerate = [Index](auto& Limits)
		{
			if (Limits.IsValidIndex(Index))
			{
				Limits[Index].Guid = FGuid::NewGuid();
			}
		};

		if (ArrayName == GET_MEMBER_NAME_CHECKED(UKawaiiPhysicsLimitsDataAsset, SphericalLimits)) Regenerate(SphericalLimits);
		else if (ArrayName == GET_MEMBER_NAME_CHECKED(UKawaiiPhysicsLimitsDataAsset, CapsuleLimits)) Regenerate(CapsuleLimits);
		else if (ArrayName == GET_MEMBER_NAME_CHECKED(UKawaiiPhysicsLimitsDataAsset, BoxLimits)) Regenerate(BoxLimits);
		else if (ArrayName == GET_MEMBER_NAME_CHECKED(UKawaiiPhysicsLimitsDataAsset, PlanarLimits)) Regenerate(PlanarLimits);
	}

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
