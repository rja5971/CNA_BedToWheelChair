// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GrabComponent.generated.h"

class UPhysicsHandleComponent;
class IIGrabbable;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnGrabStarted, AActor*, GrabbedActor, FName, BoneName, FVector, GrabLocation);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGrabEnded, AActor*, ReleasedActor);

/**
 * Grab Component — attaches to VR hands to enable physics-based grabbing.
 * 
 * Single Responsibility: Handles ONLY the grab/release mechanic.
 * Dependency Inversion: Works with IGrabbable interface, not concrete types.
 * 
 * Uses UPhysicsHandleComponent internally for smooth physics grabbing.
 */
UCLASS(ClassGroup = (PatientCare), meta = (BlueprintSpawnableComponent))
class HANDLINGRAGDOLLS_API UGrabComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGrabComponent();

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

public:
	// ============================================================
	// Grab Interface
	// ============================================================

	/** Attempt to grab whatever is in range. Call this from VR input (grip button). */
	UFUNCTION(BlueprintCallable, Category = "Ragdoll Grab")
	bool TryGrabRagdoll();

	/** Release current grab */
	UFUNCTION(BlueprintCallable, Category = "Ragdoll Grab")
	void ReleaseRagdoll();

	/** Check if currently holding something */
	UFUNCTION(BlueprintCallable, Category = "Ragdoll Grab")
	bool IsGrabbing() const { return GrabbedActor != nullptr; }

	/** Get the actor currently being grabbed */
	UFUNCTION(BlueprintCallable, Category = "Ragdoll Grab")
	AActor* GetGrabbedActor() const { return GrabbedActor; }

	/** Get the bone currently being grabbed */
	UFUNCTION(BlueprintCallable, Category = "Ragdoll Grab")
	FName GetGrabbedBone() const { return GrabbedBoneName; }

	/** Assign the motion controller/hand component used for traces and handle tracking. */
	UFUNCTION(BlueprintCallable, Category = "Ragdoll Grab")
	void SetTraceOrigin(USceneComponent* InTraceOrigin) { TraceOrigin = InTraceOrigin; }

	/** Get the configured hand component (used by belt two-hand calculations). */
	UFUNCTION(BlueprintPure, Category = "Ragdoll Grab")
	USceneComponent* GetTraceOrigin() const { return TraceOrigin; }

	// ============================================================
	// Events
	// ============================================================

	UPROPERTY(BlueprintAssignable, Category = "Grab|Events")
	FOnGrabStarted OnGrabStarted;

	UPROPERTY(BlueprintAssignable, Category = "Grab|Events")
	FOnGrabEnded OnGrabEnded;

protected:
	// ============================================================
	// Configuration
	// ============================================================

	/** How far the hand can reach to grab */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grab|Config", meta = (ClampMin = "1.0"))
	float GrabRadius = 30.0f;

	/**
	 * The scene component to trace from (should be the motion controller or hand mesh).
	 * If not set, falls back to the owner actor's root — which is WRONG for VR pawns
	 * where controllers are child components.
	 * 
	 * In Blueprint: set this to your MotionController component reference.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grab|Config")
	TObjectPtr<USceneComponent> TraceOrigin;

	/** Linear stiffness of the physics handle (how tightly it holds) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grab|Config", meta = (ClampMin = "0.0"))
	float GrabLinearStiffness = 4000.0f;

	/** Linear damping of the physics handle */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grab|Config", meta = (ClampMin = "0.0"))
	float GrabLinearDamping = 200.0f;

	/** Angular stiffness of the physics handle */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grab|Config", meta = (ClampMin = "0.0"))
	float GrabAngularStiffness = 2000.0f;

	/** Angular damping of the physics handle */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grab|Config", meta = (ClampMin = "0.0"))
	float GrabAngularDamping = 100.0f;

	/** Interpolation speed for the physics handle target update */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grab|Config", meta = (ClampMin = "0.0"))
	float GrabInterpolationSpeed = 50.0f;

private:
	/** Internal physics handle for smooth grabbing */
	UPROPERTY()
	TObjectPtr<UPhysicsHandleComponent> PhysicsHandle;

	/** Currently grabbed actor */
	UPROPERTY()
	TObjectPtr<AActor> GrabbedActor;

	/** Bone name currently grabbed */
	FName GrabbedBoneName;

	/** Location of the grab point */
	FVector GrabLocation;

	/** Find the nearest grabbable actor and bone */
	bool FindGrabTarget(AActor*& OutActor, FName& OutBoneName, FVector& OutLocation) const;

	/** Update the physics handle target to follow hand position */
	void UpdateGrabTarget();
};
