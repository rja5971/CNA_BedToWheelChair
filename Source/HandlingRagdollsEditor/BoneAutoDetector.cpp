#include "BoneAutoDetector.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"

TMap<EPatientBoneRole, FName> FBoneAutoDetector::DetectBones(const USkeletalMesh* Mesh)
{
	TMap<EPatientBoneRole, FName> Result;

	if (!Mesh)
	{
		return Result;
	}

	const FReferenceSkeleton& RefSkeleton = Mesh->GetRefSkeleton();
	const int32 NumBones = RefSkeleton.GetNum();

	if (NumBones == 0)
	{
		return Result;
	}

	// Collect all bone names with their depths for hierarchy-based matching
	struct FBoneInfo
	{
		FName Name;
		FString NameLower;
		int32 Index;
		int32 Depth;
	};

	TArray<FBoneInfo> Bones;
	Bones.Reserve(NumBones);

	for (int32 i = 0; i < NumBones; ++i)
	{
		FBoneInfo Info;
		Info.Name = RefSkeleton.GetBoneName(i);
		Info.NameLower = Info.Name.ToString().ToLower();
		Info.Index = i;
		Info.Depth = GetBoneDepth(Mesh, i);
		Bones.Add(Info);
	}

	// --- Pelvis ---
	for (const FBoneInfo& Bone : Bones)
	{
		if (Bone.NameLower.Contains(TEXT("pelvis")) || Bone.NameLower.Contains(TEXT("hip")))
		{
			// Prefer root-level pelvis (shallowest depth)
			if (!Result.Contains(EPatientBoneRole::Pelvis) || Bone.Depth < GetBoneDepth(Mesh, RefSkeleton.FindBoneIndex(Result[EPatientBoneRole::Pelvis])))
			{
				Result.Add(EPatientBoneRole::Pelvis, Bone.Name);
			}
		}
	}

	// --- Spine bones (ordered by hierarchy depth) ---
	{
		TArray<FBoneInfo> SpineBones;
		for (const FBoneInfo& Bone : Bones)
		{
			if (Bone.NameLower.Contains(TEXT("spine")))
			{
				SpineBones.Add(Bone);
			}
		}

		// Sort spine bones by depth (shallowest first)
		SpineBones.Sort([](const FBoneInfo& A, const FBoneInfo& B)
		{
			return A.Depth < B.Depth;
		});

		// Try to match numbered spine bones first
		TMap<int32, FName> NumberedSpines;
		for (const FBoneInfo& Bone : SpineBones)
		{
			for (int32 SpineNum = 1; SpineNum <= 5; ++SpineNum)
			{
				FString PatternUnderscore = FString::Printf(TEXT("spine_%02d"), SpineNum);
				FString PatternNoUnderscore = FString::Printf(TEXT("spine%d"), SpineNum);
				FString PatternUnderscore2 = FString::Printf(TEXT("spine_%d"), SpineNum);

				if (Bone.NameLower.Contains(PatternUnderscore) ||
					Bone.NameLower.Contains(PatternNoUnderscore) ||
					Bone.NameLower.Contains(PatternUnderscore2))
				{
					NumberedSpines.Add(SpineNum, Bone.Name);
					break;
				}
			}
		}

		if (NumberedSpines.Num() > 0)
		{
			// Use numbered detection
			if (NumberedSpines.Contains(1)) Result.Add(EPatientBoneRole::Spine01, NumberedSpines[1]);
			if (NumberedSpines.Contains(2)) Result.Add(EPatientBoneRole::Spine02, NumberedSpines[2]);
			if (NumberedSpines.Contains(3)) Result.Add(EPatientBoneRole::Spine03, NumberedSpines[3]);
			if (NumberedSpines.Contains(4)) Result.Add(EPatientBoneRole::Spine04, NumberedSpines[4]);
			if (NumberedSpines.Contains(5)) Result.Add(EPatientBoneRole::Spine05, NumberedSpines[5]);
		}
		else if (SpineBones.Num() > 0)
		{
			// Fallback: assign by hierarchy order
			const EPatientBoneRole SpineRoles[] = {
				EPatientBoneRole::Spine01,
				EPatientBoneRole::Spine02,
				EPatientBoneRole::Spine03,
				EPatientBoneRole::Spine04,
				EPatientBoneRole::Spine05
			};

			for (int32 i = 0; i < FMath::Min(SpineBones.Num(), 5); ++i)
			{
				Result.Add(SpineRoles[i], SpineBones[i].Name);
			}
		}
	}

	// --- Neck bones ---
	{
		TArray<FBoneInfo> NeckBones;
		for (const FBoneInfo& Bone : Bones)
		{
			if (Bone.NameLower.Contains(TEXT("neck")))
			{
				NeckBones.Add(Bone);
			}
		}

		NeckBones.Sort([](const FBoneInfo& A, const FBoneInfo& B)
		{
			return A.Depth < B.Depth;
		});

		// Try numbered detection first
		bool bFoundNumbered = false;
		for (const FBoneInfo& Bone : NeckBones)
		{
			if (Bone.NameLower.Contains(TEXT("neck_01")) || Bone.NameLower.Contains(TEXT("neck1")))
			{
				Result.Add(EPatientBoneRole::Neck01, Bone.Name);
				bFoundNumbered = true;
			}
			else if (Bone.NameLower.Contains(TEXT("neck_02")) || Bone.NameLower.Contains(TEXT("neck2")))
			{
				Result.Add(EPatientBoneRole::Neck02, Bone.Name);
				bFoundNumbered = true;
			}
		}

		if (!bFoundNumbered && NeckBones.Num() > 0)
		{
			Result.Add(EPatientBoneRole::Neck01, NeckBones[0].Name);
			if (NeckBones.Num() > 1)
			{
				Result.Add(EPatientBoneRole::Neck02, NeckBones[1].Name);
			}
		}
	}

	// --- Head ---
	for (const FBoneInfo& Bone : Bones)
	{
		if (Bone.NameLower.Contains(TEXT("head")))
		{
			Result.Add(EPatientBoneRole::Head, Bone.Name);
			break;
		}
	}

	// --- Clavicles ---
	for (const FBoneInfo& Bone : Bones)
	{
		if (Bone.NameLower.Contains(TEXT("clavicle")))
		{
			if (IsLeftBone(Bone.NameLower))
			{
				if (!Result.Contains(EPatientBoneRole::ClavicleLeft))
				{
					Result.Add(EPatientBoneRole::ClavicleLeft, Bone.Name);
				}
			}
			else if (IsRightBone(Bone.NameLower))
			{
				if (!Result.Contains(EPatientBoneRole::ClavicleRight))
				{
					Result.Add(EPatientBoneRole::ClavicleRight, Bone.Name);
				}
			}
		}
	}

	// --- Upper Arms ---
	for (const FBoneInfo& Bone : Bones)
	{
		if (Bone.NameLower.Contains(TEXT("upperarm")) || Bone.NameLower.Contains(TEXT("upper_arm")))
		{
			if (IsLeftBone(Bone.NameLower))
			{
				if (!Result.Contains(EPatientBoneRole::UpperArmLeft))
				{
					Result.Add(EPatientBoneRole::UpperArmLeft, Bone.Name);
				}
			}
			else if (IsRightBone(Bone.NameLower))
			{
				if (!Result.Contains(EPatientBoneRole::UpperArmRight))
				{
					Result.Add(EPatientBoneRole::UpperArmRight, Bone.Name);
				}
			}
		}
	}

	// --- Thighs ---
	for (const FBoneInfo& Bone : Bones)
	{
		if (Bone.NameLower.Contains(TEXT("thigh")) || Bone.NameLower.Contains(TEXT("upper_leg")) || Bone.NameLower.Contains(TEXT("upperleg")))
		{
			if (IsLeftBone(Bone.NameLower))
			{
				if (!Result.Contains(EPatientBoneRole::ThighLeft))
				{
					Result.Add(EPatientBoneRole::ThighLeft, Bone.Name);
				}
			}
			else if (IsRightBone(Bone.NameLower))
			{
				if (!Result.Contains(EPatientBoneRole::ThighRight))
				{
					Result.Add(EPatientBoneRole::ThighRight, Bone.Name);
				}
			}
		}
	}

	// --- Knees (Calf / Lower Leg) ---
	for (const FBoneInfo& Bone : Bones)
	{
		if (Bone.NameLower.Contains(TEXT("knee")) || Bone.NameLower.Contains(TEXT("calf")) || 
			Bone.NameLower.Contains(TEXT("lower_leg")) || Bone.NameLower.Contains(TEXT("lowerleg")))
		{
			if (IsLeftBone(Bone.NameLower))
			{
				if (!Result.Contains(EPatientBoneRole::KneeLeft))
				{
					Result.Add(EPatientBoneRole::KneeLeft, Bone.Name);
				}
			}
			else if (IsRightBone(Bone.NameLower))
			{
				if (!Result.Contains(EPatientBoneRole::KneeRight))
				{
					Result.Add(EPatientBoneRole::KneeRight, Bone.Name);
				}
			}
		}
	}

	return Result;
}

bool FBoneAutoDetector::IsLeftBone(const FString& BoneName)
{
	return BoneName.Contains(TEXT("_l")) || BoneName.Contains(TEXT("left")) || BoneName.Contains(TEXT(".l"));
}

bool FBoneAutoDetector::IsRightBone(const FString& BoneName)
{
	return BoneName.Contains(TEXT("_r")) || BoneName.Contains(TEXT("right")) || BoneName.Contains(TEXT(".r"));
}

TArray<FName> FBoneAutoDetector::GetAllBoneNames(const USkeletalMesh* Mesh)
{
	TArray<FName> BoneNames;

	if (!Mesh)
	{
		return BoneNames;
	}

	const FReferenceSkeleton& RefSkeleton = Mesh->GetRefSkeleton();
	const int32 NumBones = RefSkeleton.GetNum();

	BoneNames.Reserve(NumBones);
	for (int32 i = 0; i < NumBones; ++i)
	{
		BoneNames.Add(RefSkeleton.GetBoneName(i));
	}

	return BoneNames;
}

int32 FBoneAutoDetector::GetBoneDepth(const USkeletalMesh* Mesh, int32 BoneIndex)
{
	if (!Mesh || BoneIndex < 0)
	{
		return 0;
	}

	const FReferenceSkeleton& RefSkeleton = Mesh->GetRefSkeleton();
	int32 Depth = 0;
	int32 CurrentIndex = BoneIndex;

	while (CurrentIndex != INDEX_NONE)
	{
		int32 ParentIndex = RefSkeleton.GetParentIndex(CurrentIndex);
		if (ParentIndex == CurrentIndex || ParentIndex == INDEX_NONE)
		{
			break;
		}
		CurrentIndex = ParentIndex;
		++Depth;
	}

	return Depth;
}
