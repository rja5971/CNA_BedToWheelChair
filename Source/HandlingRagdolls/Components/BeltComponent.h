// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BeltComponent.generated.h"

class UGrabComponent;
class IIBeltAttachable;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBeltAttachedToPatient, AActor*, Patient);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBeltDetachedFromPatient);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnBeltHandleGrabbed, FName, HandleSide, AActor*, GrabberOwner);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBeltTwoHandGrabStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBeltTwoHandGrabEnded);

/**
 * Belt Component — the transfer belt used to safely lift patients.
 * 
 * Single Responsibility: Manages belt attachment to patient and provides lift handles.
 * Open/Closed: Different belt types can derive from this (gait belt, transfer belt, etc.)
 * 
 * NOTE: This component does NOT implement IGrabbable directly (UActorComponents cannot
 * implement UInterfaces via multiple inheritance in UE's reflection system).
 * Instead, the owning BeltActor should implement IGrabbable and delegate to this component.
 */
UCLASS(ClassGroup = (PatientCare), meta = (BlueprintSpawnableComponent))
class HANDLINGRAGDOLLS_API UBeltComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBeltComponent();

protected:
	virtual void BeginPlay() override;

public:
	// ============================================================
	// Belt Operations
	// ============================================================

	/** Attempt to attach the belt to a target actor (must implement IBeltAttachable) */
	UFUNCTION(BlueprintCallable, Category = "Belt")
	bool AttachToPatient(AActor* PatientActor);

	/** Detach the belt from the patient */
	UFUNCTION(BlueprintCallable, Category = "Belt")
	void DetachFromPatient();

	/** Check if the belt is currently attached to a patient */
	UFUNCTION(BlueprintCallable, Category = "Belt")
	bool IsAttached() const { return AttachedPatient != nullptr; }

	/** Get the patient this belt is attached to */
	UFUNCTION(BlueprintCallable, Category = "Belt")
	AActor* GetAttachedPatient() const { return AttachedPatient; }

	/** Check if the belt is currently being grabbed (for lifting) */
	UFUNCTION(BlueprintCallable, Category = "Belt")
	bool IsBeingLifted() const { return ActiveGrabbers.Num() > 0; }

	/** Get number of active grab points (2 = proper two-hand lift) */
	UFUNCTION(BlueprintCallable, Category = "Belt")
	int32 GetActiveGrabCount() const { return ActiveGrabbers.Num(); }

	/** Whether the final belt handle was released and has not yet been handled. */
	bool HasPendingFinalHandleRelease() const { return bFinalHandleReleasePending; }

	/** Consume the one-shot final-handle release request. */
	bool ConsumeFinalHandleRelease();

	// ============================================================
	// Grab Delegation (called by owning BeltActor's IGrabbable impl)
	// ============================================================

	/** Check if belt handle can be grabbed */
	bool CanHandleBeGrabbed(FName HandleName, FVector GrabLocation) const;

	/** Called when a belt handle is grabbed */
	void OnHandleGrabbed(UGrabComponent* Grabber, FName HandleName, FVector GrabLocation);

	/** Called when a belt handle is released */
	void OnHandleReleased(UGrabComponent* Grabber);

	/** Get the valid handle names */
	TArray<FName> GetHandleNames() const;

	/** Get the primitive component for physics grab */
	UPrimitiveComponent* GetBeltPrimitive() const { return BeltMesh; }

	// ============================================================
	// Events
	// ============================================================

	UPROPERTY(BlueprintAssignable, Category = "Belt|Events")
	FOnBeltAttachedToPatient OnBeltAttachedToPatient;

	UPROPERTY(BlueprintAssignable, Category = "Belt|Events")
	FOnBeltDetachedFromPatient OnBeltDetachedFromPatient;

	UPROPERTY(BlueprintAssignable, Category = "Belt|Events")
	FOnBeltHandleGrabbed OnBeltHandleGrabbed;

	/** Fires when both handles are grabbed simultaneously (two-hand grab starts) */
	UPROPERTY(BlueprintAssignable, Category = "Belt|Events")
	FOnBeltTwoHandGrabStarted OnTwoHandGrabStarted;

	/** Fires when either handle is released, breaking the two-hand grip */
	UPROPERTY(BlueprintAssignable, Category = "Belt|Events")
	FOnBeltTwoHandGrabEnded OnTwoHandGrabEnded;

	// ============================================================
	// Two-Hand Grab Position Queries (for pivot rotation)
	// ============================================================

	/** Whether both belt handles are currently being grabbed */
	UFUNCTION(BlueprintCallable, Category = "Belt")
	bool IsTwoHandGrab() const { return ActiveGrabbers.Num() >= 2; }

	/**
	 * Get the world-space positions of both grabbing hands.
	 * Returns false if not in a two-hand grab.
	 */
	UFUNCTION(BlueprintCallable, Category = "Belt")
	bool GetTwoHandGrabPositions(FVector& OutLeftPos, FVector& OutRightPos) const;

	/**
	 * Calculate the midpoint and yaw direction from two grab positions.
	 * YawDegrees is the angle of the line between hands projected onto XY plane.
	 */
	UFUNCTION(BlueprintCallable, Category = "Belt")
	float GetTwoHandYawDegrees() const;

protected:
	// ============================================================
	// Configuration
	// ============================================================

	/** Left handle bone/socket name */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Belt|Config")
	FName LeftHandleName = FName("BeltHandle_L");

	/** Right handle bone/socket name */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Belt|Config")
	FName RightHandleName = FName("BeltHandle_R");

	/** How much force is distributed across the belt vs concentrated at grab point (0-1) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Belt|Config", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ForceDistributionFactor = 0.8f;

private:
	/** The patient we're attached to */
	UPROPERTY()
	TObjectPtr<AActor> AttachedPatient;

	/** Active grabbers on this belt (hand → handle name) */
	TMap<UGrabComponent*, FName> ActiveGrabbers;

	/** Whether we were in a two-hand grab last frame (for event edge detection) */
	bool bWasTwoHandGrab = false;

	/** One-shot signal used to trigger seating when the belt is released in a chair. */
	bool bFinalHandleReleasePending = false;

	/** Mesh component of the belt (for physics interaction) */
	UPROPERTY()
	TObjectPtr<UPrimitiveComponent> BeltMesh;
};
