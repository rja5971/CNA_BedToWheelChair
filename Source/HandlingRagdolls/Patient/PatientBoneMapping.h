// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PatientBoneMapping.generated.h"

/**
 * Logical bone roles used by the simulation code.
 * The code NEVER references actual skeleton bone names directly — it references
 * these roles, and the mapping resolves them to the real bone names at runtime.
 *
 * This makes the entire system skeleton-agnostic: swap the data asset and any
 * humanoid model works.
 */
UENUM(BlueprintType)
enum class EPatientBoneRole : uint8
{
	Pelvis			UMETA(DisplayName = "Pelvis"),
	Spine01			UMETA(DisplayName = "Spine 01 (Lower)"),
	Spine02			UMETA(DisplayName = "Spine 02 (Middle)"),
	Spine03			UMETA(DisplayName = "Spine 03 (Upper)"),
	Spine04			UMETA(DisplayName = "Spine 04 (Chest)"),
	Spine05			UMETA(DisplayName = "Spine 05 (Upper Chest)"),
	Neck01			UMETA(DisplayName = "Neck 01"),
	Neck02			UMETA(DisplayName = "Neck 02"),
	Head			UMETA(DisplayName = "Head"),
	ClavicleLeft	UMETA(DisplayName = "Clavicle Left"),
	ClavicleRight	UMETA(DisplayName = "Clavicle Right"),
	UpperArmLeft	UMETA(DisplayName = "Upper Arm Left"),
	UpperArmRight	UMETA(DisplayName = "Upper Arm Right"),
	ThighLeft		UMETA(DisplayName = "Thigh Left"),
	ThighRight		UMETA(DisplayName = "Thigh Right"),
	KneeLeft		UMETA(DisplayName = "Knee Left"),
	KneeRight		UMETA(DisplayName = "Knee Right")
};

/**
 * A single mapping entry: logical role → actual skeleton bone name.
 */
USTRUCT(BlueprintType)
struct FBoneRoleEntry
{
	GENERATED_BODY()

	/** The logical role (what the code asks for) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EPatientBoneRole Role = EPatientBoneRole::Pelvis;

	/** The actual bone name in this specific skeleton */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName BoneName = NAME_None;
};

/**
 * Data Asset: maps logical bone roles to actual skeleton bone names.
 *
 * Create one of these per skeleton you want to support:
 * - DA_BoneMap_UE5Mannequin
 * - DA_BoneMap_MetaHuman
 * - DA_BoneMap_Mixamo
 * etc.
 *
 * Assign the appropriate one to PatientActor. All systems (grab, belt, spine
 * monitor, state machine) resolve bone names through this mapping.
 */
UCLASS(BlueprintType)
class HANDLINGRAGDOLLS_API UPatientBoneMapping : public UDataAsset
{
	GENERATED_BODY()

public:
	/** All bone role → name mappings */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bone Mapping")
	TArray<FBoneRoleEntry> Mappings;

	/**
	 * Resolve a logical bone role to the actual skeleton bone name.
	 * Returns NAME_None if the role is not mapped.
	 */
	UFUNCTION(BlueprintCallable, Category = "Bone Mapping")
	FName GetBoneName(EPatientBoneRole Role) const;

	/**
	 * Resolve multiple roles at once (convenience).
	 */
	UFUNCTION(BlueprintCallable, Category = "Bone Mapping")
	TArray<FName> GetBoneNames(const TArray<EPatientBoneRole>& Roles) const;

	/**
	 * Reverse lookup: given an actual bone name, find which role it maps to.
	 * Returns false if the bone is not mapped to any role.
	 */
	UFUNCTION(BlueprintCallable, Category = "Bone Mapping")
	bool GetRoleForBone(FName BoneName, EPatientBoneRole& OutRole) const;
};
