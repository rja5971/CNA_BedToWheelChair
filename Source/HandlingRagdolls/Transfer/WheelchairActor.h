// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "../Interfaces/ITransferTarget.h"
#include "WheelchairActor.generated.h"

class UBoxComponent;
class USceneComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnWheelchairTransferComplete);

/**
 * Wheelchair Actor — the transfer target for the patient.
 * 
 * Implements ITransferTarget: any transfer target can be used by the state machine.
 * Liskov Substitution: could swap in a bed, stretcher, or another chair.
 */
UCLASS(BlueprintType)
class HANDLINGRAGDOLLS_API AWheelchairActor : public AActor, public IITransferTarget
{
	GENERATED_BODY()

public:
	AWheelchairActor();

protected:
	virtual void BeginPlay() override;

public:
	// ============================================================
	// ITransferTarget Implementation
	// ============================================================
	virtual bool IsReadyToReceive() const override;
	virtual FTransform GetTargetSeatTransform() const override;
	virtual float GetAcceptanceRadius() const override;
	virtual void OnTransferBegin(AActor* Patient) override;
	virtual void OnTransferComplete(AActor* Patient) override;
	virtual void OnTransferFailed(AActor* Patient) override;

	// ============================================================
	// Wheelchair Operations
	// ============================================================

	/** Lock the wheelchair brakes (must be locked before transfer) */
	UFUNCTION(BlueprintCallable, Category = "Wheelchair")
	void LockBrakes();

	/** Unlock the wheelchair brakes */
	UFUNCTION(BlueprintCallable, Category = "Wheelchair")
	void UnlockBrakes();

	/** Check if brakes are locked */
	UFUNCTION(BlueprintCallable, Category = "Wheelchair")
	bool AreBrakesLocked() const { return bBrakesLocked; }

	/** Check if a patient is currently seated */
	UFUNCTION(BlueprintCallable, Category = "Wheelchair")
	bool IsOccupied() const { return bIsOccupied; }

	/** Whether a world-space point is inside the chair's seat area. */
	UFUNCTION(BlueprintCallable, Category = "Wheelchair")
	bool IsLocationInSeatArea(const FVector& WorldLocation, float ExtraTolerance = 8.0f) const;

	/** Broad, oriented recognition zone used to latch this chair before release. */
	UFUNCTION(BlueprintCallable, Category = "Wheelchair")
	bool IsLocationInApproachArea(const FVector& WorldLocation, float ExtraTolerance = 0.0f) const;

	/** Distance score to the exact seat target; lower is a better candidate. */
	float GetSeatDistanceSquared(const FVector& WorldLocation) const;

	/** Component that owns the final pelvis-aligned seated attachment. */
	USceneComponent* GetSeatTargetComponent() const { return SeatTarget; }

	// ============================================================
	// Events
	// ============================================================

	UPROPERTY(BlueprintAssignable, Category = "Wheelchair|Events")
	FOnWheelchairTransferComplete OnWheelchairTransferComplete;

protected:
	/** The mesh of the wheelchair */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wheelchair")
	TObjectPtr<UStaticMeshComponent> WheelchairMesh;

	/** Seat zone — when patient overlaps this, they can be seated */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wheelchair")
	TObjectPtr<UBoxComponent> SeatZone;

	/** Broad zone that immediately recognizes an approaching carried patient. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wheelchair")
	TObjectPtr<UBoxComponent> ApproachZone;

	/** Exact pelvis position; final facing follows chair forward with skeletal-axis calibration. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wheelchair")
	TObjectPtr<USceneComponent> SeatTarget;

	/** How close the patient must be to the target (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wheelchair|Config", meta = (ClampMin = "5.0"))
	float AcceptanceRadius_Config = 30.0f;

	/** Half-size of the broad chair-recognition box, in chair-local centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wheelchair|Detection")
	FVector ApproachZoneExtent = FVector(90.0f, 75.0f, 80.0f);

	/** Half-size of the final release/seat-commit box, in chair-local centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wheelchair|Detection")
	FVector SeatCommitZoneExtent = FVector(55.0f, 50.0f, 55.0f);

	/** Show the recognition boxes while playing for placement and tuning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wheelchair|Detection")
	bool bShowDetectionZones = false;

	/** Maximum velocity the patient can have when seating (cm/s) — too fast = crash */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wheelchair|Config", meta = (ClampMin = "0.0"))
	float MaxSeatingVelocity = 50.0f;

	/** Start ready for transfer. Disable when a separate brake interaction is wired. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wheelchair|Config")
	bool bStartWithBrakesLocked = true;

private:
	bool bBrakesLocked = false;
	bool bIsOccupied = false;
};
