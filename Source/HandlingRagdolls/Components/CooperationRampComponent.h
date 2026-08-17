// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CooperationRampComponent.generated.h"

class UPatientPhysicsComponent;
class UPatientBoneMapping;
class UCurveFloat;

/**
 * Cooperation Ramp Component — drives progressive "aliveness" during fold-up.
 *
 * As the nurse folds the patient upright, physical animation strength ramps
 * from Relaxed toward Cooperating, simulating a conscious patient subtly
 * engaging their muscles. Arms stay much looser than the torso.
 *
 * Extracted from PatientActor::Tick to isolate the cooperation blend logic.
 * Call TickRamp(TorsoAngle) each frame when the patient is BeingSupported.
 */
UCLASS(ClassGroup = (PatientCare), meta = (BlueprintSpawnableComponent))
class HANDLINGRAGDOLLS_API UCooperationRampComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCooperationRampComponent();

	// ============================================================
	// Initialization
	// ============================================================

	/**
	 * Initialize with required references.
	 * Must be called before TickRamp (typically in owning actor's BeginPlay).
	 */
	void Initialize(UPatientPhysicsComponent* InPhysicsComponent, UPatientBoneMapping* InBoneMapping);

	// ============================================================
	// Runtime
	// ============================================================

	/**
	 * Drive the cooperation ramp for this frame.
	 * @param TorsoAngle  Current torso upright angle in degrees (90 = flat, 0 = upright).
	 * @param SitUprightThreshold  The angle at which the patient is considered fully upright/seated.
	 */
	void TickRamp(float TorsoAngle, float SitUprightThreshold);

	// ============================================================
	// Configuration — UPROPERTY(EditAnywhere)
	// ============================================================

	/** Torso angle (degrees) at which cooperation starts blending in.
	 *  Above this angle the patient remains fully relaxed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cooperation Ramp|Thresholds",
		meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float CoopStartAngle = 70.0f;

	/** Maximum cooperation blend alpha before the patient is considered seated.
	 *  1.0 would be fully Cooperating; 0.8 keeps some looseness. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cooperation Ramp|Thresholds",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaxCoopAlpha = 0.8f;

	/** Arm orientation/force strength as a fraction of the torso strength. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cooperation Ramp|Arms",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ArmStrengthMultiplier = 0.3f;

	/** Arm angular velocity damping as a fraction of the torso damping. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cooperation Ramp|Arms",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ArmDampingMultiplier = 0.5f;

	// --- Relaxed profile values (alpha = 0) ---

	/** Orientation strength when fully relaxed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cooperation Ramp|Relaxed Values")
	float RelaxedOrientationStrength = 350.0f;

	/** Angular velocity strength when fully relaxed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cooperation Ramp|Relaxed Values")
	float RelaxedAngularVelocityStrength = 200.0f;

	/** Position strength when fully relaxed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cooperation Ramp|Relaxed Values")
	float RelaxedPositionStrength = 0.0f;

	/** Velocity strength when fully relaxed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cooperation Ramp|Relaxed Values")
	float RelaxedVelocityStrength = 0.0f;

	/** Max angular force when fully relaxed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cooperation Ramp|Relaxed Values")
	float RelaxedMaxAngularForce = 350.0f;

	/** Max linear force when fully relaxed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cooperation Ramp|Relaxed Values")
	float RelaxedMaxLinearForce = 0.0f;

	// --- Cooperating profile values (alpha = 1) ---

	/** Orientation strength when fully cooperating */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cooperation Ramp|Cooperating Values")
	float CooperatingOrientationStrength = 1200.0f;

	/** Angular velocity strength when fully cooperating */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cooperation Ramp|Cooperating Values")
	float CooperatingAngularVelocityStrength = 120.0f;

	/** Position strength when fully cooperating */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cooperation Ramp|Cooperating Values")
	float CooperatingPositionStrength = 200.0f;

	/** Velocity strength when fully cooperating */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cooperation Ramp|Cooperating Values")
	float CooperatingVelocityStrength = 50.0f;

	/** Max angular force when fully cooperating */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cooperation Ramp|Cooperating Values")
	float CooperatingMaxAngularForce = 1000.0f;

	/** Max linear force when fully cooperating */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cooperation Ramp|Cooperating Values")
	float CooperatingMaxLinearForce = 500.0f;

	// --- Optional curve override ---

	/**
	 * Optional float curve mapping torso angle → cooperation alpha.
	 * If assigned, this replaces the default linear interpolation.
	 * X axis: torso angle (degrees). Y axis: alpha (0–1, clamped to MaxCoopAlpha).
	 * If null, falls back to the linear mapping.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cooperation Ramp|Curve")
	TObjectPtr<UCurveFloat> CooperationCurve;

private:
	/** Cached reference to the patient physics component */
	UPROPERTY()
	TObjectPtr<UPatientPhysicsComponent> PhysicsComponent;

	/** Cached reference to the bone mapping data asset */
	UPROPERTY()
	TObjectPtr<UPatientBoneMapping> BoneMappingRef;
};
