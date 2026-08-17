// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "../Patient/PatientBoneMapping.h"
#include "SeatedTransitionComponent.generated.h"

class USkeletalMeshComponent;
class UPatientPhysicsComponent;

/** Broadcast when the body has settled into the seated position and is frozen */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSeatedReached);

/**
 * Seated Transition Component — Pure Physics Approach
 *
 * When the patient's torso crosses the sit threshold, this component switches
 * the physical animation profile to "Seated" (strong motors driving upright).
 * The physics system naturally settles the body into a seated posture.
 * Once the body stabilizes (angular velocity drops below threshold), all bodies
 * are frozen in place for the next step (belt attachment).
 *
 * NO ANIMATIONS NEEDED. The physics motors do all the work.
 */
UCLASS(ClassGroup = (PatientCare), meta = (BlueprintSpawnableComponent))
class HANDLINGRAGDOLLS_API USeatedTransitionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USeatedTransitionComponent();

protected:
	virtual void BeginPlay() override;

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ============================================================
	// Initialization
	// ============================================================

	void Initialize(USkeletalMeshComponent* InMesh, UPatientBoneMapping* InBoneMapping,
		UPatientPhysicsComponent* InPhysics);

	// ============================================================
	// Sit Detection
	// ============================================================

	/**
	 * Angle of the torso from vertical, in degrees (lying ≈ 90°, upright ≈ 0°).
	 */
	UFUNCTION(BlueprintCallable, Category = "Seated Transition")
	float GetTorsoUprightAngleDeg() const;

	/**
	 * Called by the owning actor's Tick. Checks whether the torso angle has passed
	 * the sit threshold and triggers the seated settle if so.
	 * Returns true if the threshold was crossed this frame.
	 */
	bool CheckSitThreshold(float TorsoAngle);

	// ============================================================
	// Seated Settle (physics-driven)
	// ============================================================

	/** Begin the physics-driven settle into seated position */
	UFUNCTION(BlueprintCallable, Category = "Seated Transition")
	void BeginSeatedSettle();

	/** Mark the patient as seated (stops sit-detection). Used when the state config
	 *  freezes the pose directly, so no motor-settle is needed. */
	UFUNCTION(BlueprintCallable, Category = "Seated Transition")
	void MarkSeated() { bSeatedLocked = true; bSettling = false; SetComponentTickEnabled(false); }

	// ============================================================
	// State Accessors
	// ============================================================

	/** Whether the patient has locked into the seated pose */
	UFUNCTION(BlueprintCallable, Category = "Seated Transition")
	bool IsSeatedLocked() const { return bSeatedLocked; }

	/** Whether we are currently settling into seated (physics motors active) */
	UFUNCTION(BlueprintCallable, Category = "Seated Transition")
	bool IsSettling() const { return bSettling; }

	// Backward compat alias
	bool IsBlendingToSeated() const { return bSettling; }

	// ============================================================
	// Events
	// ============================================================

	/** Broadcast when the body has stabilized and is frozen in the seated position */
	UPROPERTY(BlueprintAssignable, Category = "Seated Transition|Events")
	FOnSeatedReached OnSeatedReached;


	/** Broadcast when the settle was cancelled (patient fell back before stabilizing) */
	UPROPERTY(BlueprintAssignable, Category = "Seated Transition|Events")
	FOnSeatedReached OnSettleCancelled;

	// ============================================================
	// Configuration
	// ============================================================

	/**
	 * How upright the torso must become (degrees from vertical) to count as "seated".
	 * Lying flat ≈ 90°, fully upright ≈ 0°. Default 45°.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Seated Transition|Config", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float SitUprightAngleThreshold = 45.0f;

	/**
	 * Maximum angular velocity (deg/s) across all bodies to consider "settled".
	 * When all bodies drop below this, the body is frozen.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Seated Transition|Config", meta = (ClampMin = "0.0"))
	float SettleVelocityThreshold = 15.0f;

	/**
	 * Maximum time (seconds) to wait for settling. If the body hasn't stabilized
	 * by this time, force-freeze anyway. Prevents infinite waiting.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Seated Transition|Config", meta = (ClampMin = "0.5"))
	float MaxSettleTime = 3.0f;

	/**
	 * Minimum time (seconds) the body must be below SettleVelocityThreshold
	 * before we consider it truly settled (prevents premature freeze on a
	 * momentary pause during oscillation).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Seated Transition|Config", meta = (ClampMin = "0.1"))
	float MinStableTime = 0.4f;

private:
	/** Freeze all bodies in their current position */
	void FreezeInPlace();

	/** Cancel the settle and revert to Relaxed profile */
	void CancelSettle();

	/** Get the maximum angular velocity across all simulating bodies */
	float GetMaxBodyAngularVelocity() const;

	/** Resolve a logical role to an actual bone name via the BoneMapping asset */
	FName ResolveBoneName(EPatientBoneRole BoneRole) const;

	// ============================================================
	// State
	// ============================================================

	bool bSeatedLocked = false;
	bool bSettling = false;
	float SettleElapsed = 0.0f;
	float StableTime = 0.0f;

	// ============================================================
	// Cached References
	// ============================================================

	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> Mesh;

	UPROPERTY()
	TObjectPtr<UPatientBoneMapping> BoneMapping;

	UPROPERTY()
	TObjectPtr<UPatientPhysicsComponent> PhysicsComp;
};
