// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License

#include "KawaiiPhysicsBoneConstraintsDataAsset.h"

#include "Internationalization/Regex.h"

void FModifyBoneConstraintData::Update(const FModifyBoneConstraint& BoneConstraint)
{
	BoneReference1 = BoneConstraint.Bone1;
	BoneReference2 = BoneConstraint.Bone2;
	bOverrideCompliance = BoneConstraint.bOverrideCompliance;
	ComplianceType = BoneConstraint.ComplianceType;
}

void UKawaiiPhysicsBoneConstraintsDataAsset::Serialize(FStructuredArchiveRecord Record)
{
	Super::Serialize(Record);
}

void UKawaiiPhysicsBoneConstraintsDataAsset::PostLoad()
{
	Super::PostLoad();

	// BoneName1/2 predate the FBoneReference fields; carry them across once.
	for (FModifyBoneConstraintData& Data : BoneConstraintsData)
	{
		if (Data.BoneReference1.BoneName.IsNone() && !Data.BoneName1.IsNone())
		{
			Data.BoneReference1.BoneName = Data.BoneName1;
		}

		if (Data.BoneReference2.BoneName.IsNone() && !Data.BoneName2.IsNone())
		{
			Data.BoneReference2.BoneName = Data.BoneName2;
		}
	}
}

USkeleton* UKawaiiPhysicsBoneConstraintsDataAsset::GetSkeleton(bool& bInvalidSkeletonIsError,
                                                               const IPropertyHandle* PropertyHandle)
{
	bInvalidSkeletonIsError = false;

#if WITH_EDITORONLY_DATA
	return PreviewSkeleton.LoadSynchronous();
#else
	return nullptr;
#endif
}

TArray<FModifyBoneConstraint> UKawaiiPhysicsBoneConstraintsDataAsset::GenerateBoneConstraints()
{
	TArray<FModifyBoneConstraint> Constraints;
	Constraints.Reserve(BoneConstraintsData.Num());

	for (const FModifyBoneConstraintData& Data : BoneConstraintsData)
	{
		if (Data.BoneReference1.BoneName.IsNone() || Data.BoneReference2.BoneName.IsNone())
		{
			continue;
		}

		FModifyBoneConstraint Constraint;
		Constraint.Bone1 = Data.BoneReference1;
		Constraint.Bone2 = Data.BoneReference2;
		Constraint.bOverrideCompliance = Data.bOverrideCompliance;
		Constraint.ComplianceType = Data.ComplianceType;

		Constraints.Add(Constraint);
	}

	return Constraints;
}

#if WITH_EDITOR

void UKawaiiPhysicsBoneConstraintsDataAsset::ApplyRegex()
{
	UpdatePreviewBoneList();

	// Each pattern pair walks the skeleton once; bones matching pattern 1 are
	// paired with the bone at the same match ordinal from pattern 2. That keeps
	// left/right or front/back skirt panels lined up without hand-authoring.
	for (const FRegexPatternBoneSet& PatternSet : RegexPatternList)
	{
		if (PatternSet.RegexPatternBone1.IsEmpty() || PatternSet.RegexPatternBone2.IsEmpty())
		{
			continue;
		}

		TArray<FName> Matches1;
		TArray<FName> Matches2;

		for (const FBoneReference& Bone : PreviewBoneList)
		{
			const FString BoneName = Bone.BoneName.ToString();

			FRegexPattern Pattern1(PatternSet.RegexPatternBone1);
			if (FRegexMatcher Matcher1(Pattern1, BoneName); Matcher1.FindNext())
			{
				Matches1.Add(Bone.BoneName);
			}

			FRegexPattern Pattern2(PatternSet.RegexPatternBone2);
			if (FRegexMatcher Matcher2(Pattern2, BoneName); Matcher2.FindNext())
			{
				Matches2.Add(Bone.BoneName);
			}
		}

		const int32 PairCount = FMath::Min(Matches1.Num(), Matches2.Num());
		for (int32 Index = 0; Index < PairCount; ++Index)
		{
			if (Matches1[Index] == Matches2[Index])
			{
				continue;
			}

			const bool bAlreadyPresent = BoneConstraintsData.ContainsByPredicate(
				[&](const FModifyBoneConstraintData& Existing)
				{
					return (Existing.BoneReference1.BoneName == Matches1[Index] &&
							Existing.BoneReference2.BoneName == Matches2[Index]) ||
						(Existing.BoneReference1.BoneName == Matches2[Index] &&
							Existing.BoneReference2.BoneName == Matches1[Index]);
				});

			if (bAlreadyPresent)
			{
				continue;
			}

			FModifyBoneConstraintData NewData;
			NewData.BoneReference1.BoneName = Matches1[Index];
			NewData.BoneReference2.BoneName = Matches2[Index];
			BoneConstraintsData.Add(NewData);
		}
	}

	MarkPackageDirty();
}

void UKawaiiPhysicsBoneConstraintsDataAsset::UpdatePreviewBoneList()
{
	PreviewBoneList.Empty();
	PreviewBoneListString.Empty();

	const USkeleton* LoadedSkeleton = PreviewSkeleton.LoadSynchronous();
	if (!LoadedSkeleton)
	{
		return;
	}

	const FReferenceSkeleton& RefSkeleton = LoadedSkeleton->GetReferenceSkeleton();
	PreviewBoneList.Reserve(RefSkeleton.GetNum());

	TStringBuilder<4096> Builder;
	for (int32 Index = 0; Index < RefSkeleton.GetNum(); ++Index)
	{
		FBoneReference Bone;
		Bone.BoneName = RefSkeleton.GetBoneName(Index);
		PreviewBoneList.Add(Bone);

		Builder << Bone.BoneName.ToString() << TEXT("\n");
	}

	PreviewBoneListString = Builder.ToString();
}

void UKawaiiPhysicsBoneConstraintsDataAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UKawaiiPhysicsBoneConstraintsDataAsset, PreviewSkeleton))
	{
		UpdatePreviewBoneList();
	}
}

#endif
