// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License

#pragma once

#include "CoreMinimal.h"
#include "AnimNode_KawaiiPhysics.h"
#include "Engine/DataAsset.h"
#include "Interfaces/Interface_BoneReferenceSkeletonProvider.h"
#include "KawaiiPhysicsLimitsDataAsset.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnLimitsChanged, struct FPropertyChangedEvent&);

/**
 * Pre-refactor storage for a collision limit.
 *
 * Superseded by storing FCollisionLimitBase directly, but still reflected --
 * the game's schema carries these, and old data assets deserialize through
 * them before PostLoad converts them across.
 */
USTRUCT()
struct FCollisionLimitDataBase
{
	GENERATED_BODY()

	/** [0] */
	UPROPERTY(meta = (DeprecatedProperty))
	FBoneReference DrivingBoneReference;

	/** [1] */
	UPROPERTY(meta = (DeprecatedProperty))
	FName DrivingBoneName;

	/** [2] */
	UPROPERTY(meta = (DeprecatedProperty))
	FVector OffsetLocation = FVector::ZeroVector;

	/** [3] */
	UPROPERTY(meta = (DeprecatedProperty))
	FRotator OffsetRotation = FRotator::ZeroRotator;

	/** [4] */
	UPROPERTY(meta = (DeprecatedProperty))
	FVector Location = FVector::ZeroVector;

	/** [5] */
	UPROPERTY(meta = (DeprecatedProperty))
	FQuat Rotation = FQuat::Identity;

	/** [6] */
	UPROPERTY(meta = (DeprecatedProperty, IgnoreForMemberInitializationTest))
	FGuid Guid = FGuid::NewGuid();

protected:
	void ConvertBase(FCollisionLimitBase& Limit) const
	{
		Limit.DrivingBone.BoneName = DrivingBoneReference.BoneName;
		Limit.OffsetLocation = OffsetLocation;
		Limit.OffsetRotation = OffsetRotation;
		Limit.Location = Location;
		Limit.Rotation = Rotation;
		Limit.SourceType = ECollisionSourceType::DataAsset;

#if WITH_EDITORONLY_DATA
		Limit.Guid = Guid;
#endif
	}
};

USTRUCT()
struct FSphericalLimitData : public FCollisionLimitDataBase
{
	GENERATED_BODY()

	/** [0] */
	UPROPERTY(meta = (DeprecatedProperty))
	float Radius = 5.0f;

	/** [1] */
	UPROPERTY(meta = (DeprecatedProperty))
	ESphericalLimitType LimitType = ESphericalLimitType::Outer;

	FSphericalLimit Convert() const
	{
		FSphericalLimit Limit;
		ConvertBase(Limit);
		Limit.Radius = Radius;
		Limit.LimitType = LimitType;
		return Limit;
	}
};

USTRUCT()
struct FCapsuleLimitData : public FCollisionLimitDataBase
{
	GENERATED_BODY()

	/** [0] */
	UPROPERTY(meta = (DeprecatedProperty))
	float Radius = 5.0f;

	/** [1] */
	UPROPERTY(meta = (DeprecatedProperty))
	float Length = 10.0f;

	FCapsuleLimit Convert() const
	{
		FCapsuleLimit Limit;
		ConvertBase(Limit);
		Limit.Radius = Radius;
		Limit.Length = Length;
		return Limit;
	}
};

USTRUCT()
struct FBoxLimitData : public FCollisionLimitDataBase
{
	GENERATED_BODY()

	/** [0] */
	UPROPERTY(meta = (DeprecatedProperty))
	FVector Extent = FVector(5.0f, 5.0f, 5.0f);

	FBoxLimit Convert() const
	{
		FBoxLimit Limit;
		ConvertBase(Limit);
		Limit.Extent = Extent;
		return Limit;
	}
};

USTRUCT()
struct FPlanarLimitData : public FCollisionLimitDataBase
{
	GENERATED_BODY()

	/** [0] */
	UPROPERTY(meta = (DeprecatedProperty))
	FPlane Plane = FPlane(0, 0, 0, 0);

	FPlanarLimit Convert() const
	{
		FPlanarLimit Limit;
		ConvertBase(Limit);
		Limit.Plane = Plane;
		return Limit;
	}
};

/**
 * A shareable set of collision limits.
 *
 * NOTE: the four runtime arrays must stay last. Everything above them is
 * editor-only and disappears on cook, which is what makes SphericalLimits land
 * on schema index 0 in the shipped game.
 */
UCLASS(Blueprintable)
class KAWAIIPHYSICS_API UKawaiiPhysicsLimitsDataAsset : public UDataAsset, public IBoneReferenceSkeletonProvider
{
	GENERATED_BODY()

public:
#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skeleton")
	TObjectPtr<USkeleton> Skeleton;

	UPROPERTY(meta = (DeprecatedProperty))
	TArray<FSphericalLimitData> SphericalLimitsData;
	UPROPERTY(meta = (DeprecatedProperty))
	TArray<FCapsuleLimitData> CapsuleLimitsData;
	UPROPERTY(meta = (DeprecatedProperty))
	TArray<FBoxLimitData> BoxLimitsData;
	UPROPERTY(meta = (DeprecatedProperty))
	TArray<FPlanarLimitData> PlanarLimitsData;
#endif

	/** [0] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spherical Limits")
	TArray<FSphericalLimit> SphericalLimits;

	/** [1] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capsule Limits")
	TArray<FCapsuleLimit> CapsuleLimits;

	/** [2] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box Limits")
	TArray<FBoxLimit> BoxLimits;

	/** [3] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planar Limits")
	TArray<FPlanarLimit> PlanarLimits;

	// Begin UObject Interface.
#if WITH_EDITORONLY_DATA
	virtual void Serialize(FStructuredArchiveRecord Record) override;
#endif
	virtual void PostLoad() override;
	// End UObject Interface.

	// IBoneReferenceSkeletonProvider interface
	virtual USkeleton* GetSkeleton(bool& bInvalidSkeletonIsError, const IPropertyHandle* PropertyHandle) override;

#if WITH_EDITOR
	void UpdateLimit(FCollisionLimitBase* Limit);

	FOnLimitsChanged OnLimitsChanged;
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif

private:
#if WITH_EDITOR
	void Sync();
#endif
};
