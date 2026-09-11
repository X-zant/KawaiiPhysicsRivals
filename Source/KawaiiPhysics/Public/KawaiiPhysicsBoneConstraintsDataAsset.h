// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License

#pragma once

#include "CoreMinimal.h"
#include "AnimNode_KawaiiPhysics.h"
#include "Engine/DataAsset.h"
#include "Interfaces/Interface_BoneReferenceSkeletonProvider.h"
#include "KawaiiPhysicsBoneConstraintsDataAsset.generated.h"

/** A bone pair as stored in a data asset, before it is resolved against a chain. */
USTRUCT(BlueprintType)
struct KAWAIIPHYSICS_API FModifyBoneConstraintData
{
	GENERATED_BODY()

	/** [0] Superseded by BoneReference1; kept so old assets still load. */
	UPROPERTY()
	FName BoneName1;

	/** [1] */
	UPROPERTY()
	FName BoneName2;

	/** [2] */
	UPROPERTY(EditAnywhere, Category = "KawaiiPhysics")
	FBoneReference BoneReference1;

	/** [3] */
	UPROPERTY(EditAnywhere, Category = "KawaiiPhysics")
	FBoneReference BoneReference2;

	/** [4] */
	UPROPERTY(EditAnywhere, Category = "KawaiiPhysics", meta = (InlineEditConditionToggle))
	bool bOverrideCompliance = false;

	/** [5] */
	UPROPERTY(EditAnywhere, Category = "KawaiiPhysics", meta = (EditCondition = "bOverrideCompliance"))
	EXPBDComplianceType ComplianceType = EXPBDComplianceType::Leather;

	void Update(const FModifyBoneConstraint& BoneConstraint);
};

/** Regex pair used to bulk-generate constraints from a skeleton's bone names. */
USTRUCT(BlueprintType)
struct FRegexPatternBoneSet
{
	GENERATED_BODY()

	/** [0] */
	UPROPERTY(EditAnywhere, Category = "Helper")
	FString RegexPatternBone1;

	/** [1] */
	UPROPERTY(EditAnywhere, Category = "Helper")
	FString RegexPatternBone2;
};

/** A shareable set of XPBD bone constraints. */
UCLASS(Blueprintable)
class KAWAIIPHYSICS_API UKawaiiPhysicsBoneConstraintsDataAsset : public UDataAsset,
                                                                 public IBoneReferenceSkeletonProvider
{
	GENERATED_BODY()

public:
	/** [0] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bone Constraint",
		meta = (TitleProperty = "{BoneReference1} - {BoneReference2}"))
	TArray<FModifyBoneConstraintData> BoneConstraintsData;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = "Helper")
	TArray<FRegexPatternBoneSet> RegexPatternList;

	UPROPERTY(EditAnywhere, Category = "Skeleton")
	TSoftObjectPtr<USkeleton> PreviewSkeleton;

	UPROPERTY(VisibleAnywhere, Category = "Skeleton", meta = (EditCondition = false))
	TArray<FBoneReference> PreviewBoneList;

	UPROPERTY()
	FString PreviewBoneListString;
#endif

	// Begin UObject Interface.
	virtual void Serialize(FStructuredArchiveRecord Record) override;
	virtual void PostLoad() override;
	// End UObject Interface.

	// IBoneReferenceSkeletonProvider interface
	virtual USkeleton* GetSkeleton(bool& bInvalidSkeletonIsError, const IPropertyHandle* PropertyHandle) override;

	TArray<FModifyBoneConstraint> GenerateBoneConstraints();

#if WITH_EDITOR
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Helper")
	void ApplyRegex();

	void UpdatePreviewBoneList();

	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
