// Fill out your copyright notice in the Description page of Project Settings.

#include "PatientAnimGenerator.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Engine/SkeletalMesh.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "ReferenceSkeleton.h"

UAnimSequence* UPatientAnimGenerator::GenerateSitUpAnimation(USkeletalMesh* Mesh, int32 NumFrames, FString SavePath)
{
	if (!Mesh)
	{
		UE_LOG(LogTemp, Error, TEXT("PatientAnimGenerator: No SkeletalMesh provided!"));
		return nullptr;
	}

	USkeleton* Skeleton = Mesh->GetSkeleton();
	if (!Skeleton)
	{
		UE_LOG(LogTemp, Error, TEXT("PatientAnimGenerator: SkeletalMesh has no Skeleton!"));
		return nullptr;
	}

	// --- Create the AnimSequence package and asset ---
	FString PackagePath = SavePath;
	FString AssetName = FPackageName::GetShortName(PackagePath);

	// Ensure the package path is valid
	FString PackageName = PackagePath;
	if (!PackageName.StartsWith(TEXT("/")))
	{
		PackageName = TEXT("/Game/") + PackageName;
	}

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		UE_LOG(LogTemp, Error, TEXT("PatientAnimGenerator: Failed to create package at '%s'"), *PackageName);
		return nullptr;
	}

	UAnimSequence* AnimSeq = NewObject<UAnimSequence>(Package, *AssetName, RF_Public | RF_Standalone);
	if (!AnimSeq)
	{
		UE_LOG(LogTemp, Error, TEXT("PatientAnimGenerator: Failed to create AnimSequence!"));
		return nullptr;
	}

	AnimSeq->SetSkeleton(Skeleton);

	// --- Configure the animation via the DataController ---
	IAnimationDataController& Controller = AnimSeq->GetController();
	
	Controller.OpenBracket(FText::FromString(TEXT("Generate Sit-Up Animation")), false);
	
	// Set frame rate and length
	const float FrameRate = 30.0f;
	Controller.SetFrameRate(FFrameRate(30, 1), false);
	Controller.SetNumberOfFrames(FFrameNumber(NumFrames), false);

	// --- Find spine bones in the skeleton ---
	const FReferenceSkeleton& RefSkeleton = Mesh->GetRefSkeleton();
	
	// Bone names to animate (spine chain that bends forward for sitting)
	// We search case-insensitively for common patterns
	TArray<FName> SpineBones;
	TArray<int32> SpineBoneIndices;
	
	for (int32 i = 0; i < RefSkeleton.GetNum(); i++)
	{
		FString BoneName = RefSkeleton.GetBoneName(i).ToString().ToLower();
		if (BoneName.Contains(TEXT("spine")))
		{
			SpineBones.Add(RefSkeleton.GetBoneName(i));
			SpineBoneIndices.Add(i);
		}
	}

	// Also find neck bones
	TArray<FName> NeckBones;
	for (int32 i = 0; i < RefSkeleton.GetNum(); i++)
	{
		FString BoneName = RefSkeleton.GetBoneName(i).ToString().ToLower();
		if (BoneName.Contains(TEXT("neck")))
		{
			NeckBones.Add(RefSkeleton.GetBoneName(i));
		}
	}

	if (SpineBones.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("PatientAnimGenerator: No spine bones found in skeleton!"));
		Controller.CloseBracket(false);
		return nullptr;
	}

	UE_LOG(LogTemp, Log, TEXT("PatientAnimGenerator: Found %d spine bones and %d neck bones"), SpineBones.Num(), NeckBones.Num());

	// --- Calculate rotation per bone ---
	// Total forward bend: ~50° distributed across spine bones
	// Start at ~45° bent, end at ~90° (seated) = 45° of additional forward rotation
	// distributed across the spine chain
	const float TotalBendDegrees = 45.0f;
	const float BendPerSpineBone = TotalBendDegrees / FMath::Max(SpineBones.Num(), 1);

	// For each spine bone, generate keyframes
	for (int32 BoneIdx = 0; BoneIdx < SpineBones.Num(); BoneIdx++)
	{
		FName BoneName = SpineBones[BoneIdx];
		
		// Add the bone curve
		Controller.AddBoneCurve(BoneName, false);

		// Generate keys: rotation ramps from identity (frame 0) to the target bend (last frame)
		TArray<FVector3f> PosKeys;
		TArray<FQuat4f> RotKeys;
		TArray<FVector3f> ScaleKeys;

		for (int32 Frame = 0; Frame <= NumFrames; Frame++)
		{
			float Alpha = (float)Frame / (float)NumFrames;
			// EaseInOut for natural motion
			Alpha = FMath::InterpEaseInOut(0.0f, 1.0f, Alpha, 2.0f);

			// Rotation: bend forward (positive X rotation in local space = forward flexion)
			float BendAngle = BendPerSpineBone * Alpha;
			FQuat4f BoneRot = FQuat4f(FRotator3f(0.0f, BendAngle, 0.0f).Quaternion());
			// Note: In UE5, Pitch is Y-axis rotation for forward/back bend
			// The actual axis depends on the skeleton. For UE5 Mannequin:
			// Forward flexion is typically around the Y axis (Pitch)
			BoneRot = FQuat4f(FVector3f(0.0f, 1.0f, 0.0f), FMath::DegreesToRadians(BendAngle));

			PosKeys.Add(FVector3f::ZeroVector); // No translation change
			RotKeys.Add(BoneRot);
			ScaleKeys.Add(FVector3f::OneVector); // No scale change
		}

		Controller.SetBoneTrackKeys(BoneName, PosKeys, RotKeys, ScaleKeys, false);
	}

	// Neck bones: slight compensating extension (head stays relatively level)
	for (int32 BoneIdx = 0; BoneIdx < NeckBones.Num(); BoneIdx++)
	{
		FName BoneName = NeckBones[BoneIdx];
		Controller.AddBoneCurve(BoneName, false);

		float NeckCompensation = -5.0f; // Slight backward tilt to keep head level

		TArray<FVector3f> PosKeys;
		TArray<FQuat4f> RotKeys;
		TArray<FVector3f> ScaleKeys;

		for (int32 Frame = 0; Frame <= NumFrames; Frame++)
		{
			float Alpha = (float)Frame / (float)NumFrames;
			Alpha = FMath::InterpEaseInOut(0.0f, 1.0f, Alpha, 2.0f);

			float BendAngle = NeckCompensation * Alpha;
			FQuat4f BoneRot = FQuat4f(FVector3f(0.0f, 1.0f, 0.0f), FMath::DegreesToRadians(BendAngle));

			PosKeys.Add(FVector3f::ZeroVector);
			RotKeys.Add(BoneRot);
			ScaleKeys.Add(FVector3f::OneVector);
		}

		Controller.SetBoneTrackKeys(BoneName, PosKeys, RotKeys, ScaleKeys, false);
	}

	Controller.CloseBracket(false);

	// --- Save the asset ---
	AnimSeq->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(AnimSeq);
	
	FString PackageFileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	UPackage::SavePackage(Package, AnimSeq, *PackageFileName, SaveArgs);

	UE_LOG(LogTemp, Log, TEXT("PatientAnimGenerator: Created sit-up animation '%s' with %d frames, %d spine bones animated"),
		*PackageName, NumFrames, SpineBones.Num());

	return AnimSeq;
}
