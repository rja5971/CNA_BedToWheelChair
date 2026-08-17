// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "../Interfaces/ITransferTarget.h"
#include "WheelchairActor.generated.h"

class UBoxComponent;

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

	/** The exact position/rotation the patient should end up in */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wheelchair")
	TObjectPtr<USceneComponent> SeatTarget;

	/** How close the patient must be to the target (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wheelchair|Config", meta = (ClampMin = "5.0"))
	float AcceptanceRadius_Config = 30.0f;

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
