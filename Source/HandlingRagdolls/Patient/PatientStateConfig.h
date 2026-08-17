// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PatientTypes.h"
#include "PatientStateConfig.generated.h"

/**
 * How a bone group behaves physically in a given patient state.
 */
UENUM(BlueprintType)
enum class EBoneBehavior : uint8
{
	/** Kinematic — fixed in place, cannot be moved (e.g., pelvis anchored to the bed) */
	Anchored	UMETA(DisplayName = "Anchored (Kinematic / Fixed)"),

	/** Strong motor drive — holds the animation pose, feels like a conscious tensed muscle */
	Stiff		UMETA(DisplayName = "Stiff (Strong Motor / Conscious)"),

	/** Little to no motor — yields to gravity and hands, ragdoll-like (relaxed / limp limb) */
	Free		UMETA(DisplayName = "Free (Ragdoll / Relaxed)"),

	/**
	 * Position-pinned but yaw-rotation-free. The bone holds its XYZ world position
	 * and its pitch/roll, but yaw can be driven externally (e.g., by hand positions
	 * during a belt pivot transfer). Used for the pelvis during seated rotation.
	 */
	Pivot		UMETA(DisplayName = "Pivot (Position Locked, Yaw Free)")
};

/**
 * Logical bone groups. Each maps to a subtree root bone (resolved via BoneMapping).
 * Groups are applied in the order listed in the state config — list BROAD groups
 * first (WholeBody, Spine) and NARROW groups after (Arms, Neck, Head) so the
 * narrow ones override the broad ones for their subtrees.
 */
UENUM(BlueprintType)
enum class EPatientBoneGroup : uint8
{
	/** Everything below the pelvis (whole body, pelvis excluded) */
	WholeBodyBelowPelvis	UMETA(DisplayName = "Whole Body (below pelvis)"),
	/** The pelvis only — the anchor point */
	Pelvis					UMETA(DisplayName = "Pelvis"),
	/** Spine chain and everything above it (neck, head, arms) */
	Spine					UMETA(DisplayName = "Spine (+ above)"),
	/** Neck and head */
	Neck					UMETA(DisplayName = "Neck (+ head)"),
	/** Head only */
	Head					UMETA(DisplayName = "Head"),
	/** Left arm (clavicle down) */
	LeftArm					UMETA(DisplayName = "Left Arm"),
	/** Right arm (clavicle down) */
	RightArm				UMETA(DisplayName = "Right Arm"),
	/** Left leg (thigh down) */
	LeftLeg					UMETA(DisplayName = "Left Leg"),
	/** Right leg (thigh down) */
	RightLeg				UMETA(DisplayName = "Right Leg")
};

/**
 * A single bone group's behavior within a state.
 */
USTRUCT(BlueprintType)
struct FBoneGroupBehavior
{
	GENERATED_BODY()

	/** Which bone group this entry configures */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EPatientBoneGroup Group = EPatientBoneGroup::WholeBodyBelowPelvis;

	/** How that group behaves */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EBoneBehavior Behavior = EBoneBehavior::Free;

	/**
	 * Optional orientation-strength override for Stiff/Free. If < 0, uses the
	 * state config's default for that behavior. Lets you fine-tune a specific group
	 * (e.g., neck slightly stiffer than the rest).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "-1.0"))
	float OrientationStrengthOverride = -1.0f;
};

/**
 * Patient State Config — a data asset defining how the patient's body behaves
 * physically in a specific state.
 *
 * Each state (LyingDown, BeingSupported, Seated, etc.) can have its own config
 * specifying which bone groups are Anchored, Stiff, or Free. This makes the whole
 * simulation data-driven: designers add/tune states without touching code.
 *
 * Open/Closed: create new configs for new states or new patient types.
 */
UCLASS(BlueprintType)
class HANDLINGRAGDOLLS_API UPatientStateConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Which patient state this config applies to */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "State Config")
	EPatientState State = EPatientState::LyingDown;

	/**
	 * Default behavior applied to EVERY bone group first, before the explicit
	 * BoneGroups overrides below. This makes each config fully deterministic —
	 * groups you don't list fall back to this instead of leftover state from a
	 * previous config. Default = Free (ragdoll).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "State Config")
	EBoneBehavior DefaultBehavior = EBoneBehavior::Free;

	/**
	 * Bone group behaviors, applied in order. List BROAD groups first
	 * (WholeBody, Spine) and NARROW after (Arms, Neck, Head) so narrow
	 * groups override broad ones for their subtrees.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "State Config")
	TArray<FBoneGroupBehavior> BoneGroups;

	// ============================================================
	// Default motor strengths (used when a group doesn't override)
	// ============================================================

	/** Orientation strength for Stiff groups (strong = holds pose) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "State Config|Stiff", meta = (ClampMin = "0.0"))
	float StiffOrientationStrength = 2000.0f;

	/** Angular velocity damping for Stiff groups */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "State Config|Stiff", meta = (ClampMin = "0.0"))
	float StiffAngularVelocityStrength = 200.0f;

	/** Max angular force for Stiff groups */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "State Config|Stiff", meta = (ClampMin = "0.0"))
	float StiffMaxAngularForce = 1500.0f;

	/** Orientation strength for Free groups (low/zero = ragdoll). Default 0 = pure ragdoll. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "State Config|Free", meta = (ClampMin = "0.0"))
	float FreeOrientationStrength = 0.0f;

	/** Angular velocity damping for Free groups (a little damping stops wild flailing) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "State Config|Free", meta = (ClampMin = "0.0"))
	float FreeAngularVelocityStrength = 50.0f;
};
