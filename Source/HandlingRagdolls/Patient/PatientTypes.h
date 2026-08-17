// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PatientTypes.generated.h"

/**
 * Body regions that can be interacted with on the patient.
 */
UENUM(BlueprintType)
enum class EPatientBodyPart : uint8
{
	None		UMETA(DisplayName = "None"),
	Head		UMETA(DisplayName = "Head"),
	Neck		UMETA(DisplayName = "Neck"),
	UpperSpine	UMETA(DisplayName = "Upper Spine"),
	MiddleSpine	UMETA(DisplayName = "Middle Spine"),
	LowerSpine	UMETA(DisplayName = "Lower Spine"),
	Pelvis		UMETA(DisplayName = "Pelvis"),
	LeftArm		UMETA(DisplayName = "Left Arm"),
	RightArm	UMETA(DisplayName = "Right Arm"),
	LeftLeg		UMETA(DisplayName = "Left Leg"),
	RightLeg	UMETA(DisplayName = "Right Leg")
};

/**
 * Configuration for a single spine bone's constraints and thresholds.
 */
USTRUCT(BlueprintType)
struct FSpineBoneConfig
{
	GENERATED_BODY()

	/** The bone name in the skeleton */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName BoneName;

	/** Maximum safe angular deviation in degrees (twist) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float SafeTwistAngle = 5.0f;

	/** Maximum safe angular deviation in degrees (swing) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float SafeSwingAngle = 8.0f;

	/** Warning threshold as percentage of safe angle (0-1) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WarningThreshold = 0.7f;

	/** Maximum safe angular velocity (degrees/sec) — too fast = dangerous */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float MaxSafeAngularVelocity = 30.0f;

	/** Damage multiplier for this bone (cervical spine is more critical than thoracic) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.1"))
	float DamageMultiplier = 1.0f;
};

/**
 * Identifies which physical animation profile is active.
 * Profiles control how strongly the patient's body is driven toward its
 * keyframed animation pose (muscle tone simulation).
 */
UENUM(BlueprintType)
enum class EPhysicalAnimProfile : uint8
{
	Relaxed		UMETA(DisplayName = "Relaxed (Lying Down)"),
	Cooperating	UMETA(DisplayName = "Cooperating (Sit Up / Lift)"),
	Seated		UMETA(DisplayName = "Seated (Stiff Lower Body)"),
	Limp		UMETA(DisplayName = "Limp (Unconscious / Injured)")
};

/**
 * Defines the drive strength parameters for the PhysicalAnimationComponent.
 * These map directly to FPhysicalAnimationData used by Unreal's
 * ApplyPhysicalAnimationSettingsBelow().
 *
 * Higher strength = body more rigidly follows the animation pose.
 * Lower strength = body yields more to external forces (nurse's hands, gravity).
 */
USTRUCT(BlueprintType)
struct FPhysicalAnimProfileData
{
	GENERATED_BODY()

	/** Which profile this data represents */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EPhysicalAnimProfile Profile = EPhysicalAnimProfile::Relaxed;

	/** The root bone from which this profile is applied downward the hierarchy.
	 *  Leave empty to use the patient's resolved Pelvis bone from BoneMapping. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName DriveBelowBone = NAME_None;

	/** Strength driving bones toward their target orientation (angular) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float OrientationStrength = 1000.0f;

	/** Damping on angular velocity */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float AngularVelocityStrength = 100.0f;

	/** Strength driving bones toward their target position (linear, world space) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float PositionStrength = 0.0f;

	/** Damping on linear velocity (world space) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float VelocityStrength = 0.0f;

	/** Max linear force applied (0 = unlimited) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float MaxLinearForce = 0.0f;

	/** Max angular force applied (0 = unlimited) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float MaxAngularForce = 0.0f;

	/** If true, apply this profile in world space rather than local body space */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool bIsLocalSimulation = true;
};

/**
 * Data asset containing all spine constraint configuration for a patient.
 * Allows designers to tweak injury severity without code changes.
 * Open/Closed: create different data assets for different injury types.
 */
UCLASS(BlueprintType)
class HANDLINGRAGDOLLS_API USpineConstraintConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	/** All spine bones and their constraints */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spine Config")
	TArray<FSpineBoneConfig> SpineBones;

	/** Overall damage threshold — if accumulated stress exceeds this, simulation fails */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spine Config", meta = (ClampMin = "0.0"))
	float DamageThreshold = 100.0f;

	/** How quickly stress accumulates when exceeding safe angles (per second) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spine Config", meta = (ClampMin = "0.0"))
	float StressAccumulationRate = 10.0f;

	/** How quickly stress decays when within safe limits (per second) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spine Config", meta = (ClampMin = "0.0"))
	float StressDecayRate = 2.0f;

	/** Patient mass in kg — affects how much force is needed to move them */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Patient Properties", meta = (ClampMin = "30.0"))
	float PatientMassKg = 70.0f;

	/** Description of the injury for UI/feedback purposes */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Patient Properties")
	FText InjuryDescription;

	/**
	 * Physical animation profiles that control muscle-tone simulation.
	 * Add one entry per EPhysicalAnimProfile you want to support.
	 * These are applied via UPhysicalAnimationComponent to make the patient
	 * behave like a conscious human rather than a limp ragdoll.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physical Animation")
	TArray<FPhysicalAnimProfileData> PhysicalAnimProfiles;
};

/**
 * Current state of the patient during the simulation.
 */
UENUM(BlueprintType)
enum class EPatientState : uint8
{
	LyingDown		UMETA(DisplayName = "Lying Down"),
	BeingSupported	UMETA(DisplayName = "Being Supported"),
	BeltAttached	UMETA(DisplayName = "Belt Attached"),
	BeingLifted		UMETA(DisplayName = "Being Lifted"),
	BeingTransferred UMETA(DisplayName = "Being Transferred"),
	Seated			UMETA(DisplayName = "Seated"),
	Injured			UMETA(DisplayName = "Injured - Failed")
};
