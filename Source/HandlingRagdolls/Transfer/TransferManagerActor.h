// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TransferManagerActor.generated.h"

class UTransferStateMachine;
class UScoringComponent;
class USpineMonitorComponent;
class APatientActor;
class AWheelchairActor;
class ABeltActor;
class UVRPatientCareBridgeComponent;

/**
 * Transfer Manager Actor — the single orchestrator placed in the level.
 *
 * Owns the transfer state machine, scoring, and spine monitor components.
 * Wire the Patient, Wheelchair, and Belt references in the editor details panel,
 * then call StartTransfer() to begin the task.
 *
 * This keeps game logic off the individual actors (Single Responsibility) while
 * providing a central coordination point.
 */
UCLASS(BlueprintType)
class HANDLINGRAGDOLLS_API ATransferManagerActor : public AActor
{
	GENERATED_BODY()

public:
	ATransferManagerActor();

protected:
	virtual void BeginPlay() override;

public:
	/** Start the transfer task */
	UFUNCTION(BlueprintCallable, Category = "Transfer Manager")
	void StartTransfer();

	/** Reset the state machine and scoring */
	UFUNCTION(BlueprintCallable, Category = "Transfer Manager")
	void ResetTransfer();

	/** Get current instructions text (for HUD) */
	UFUNCTION(BlueprintCallable, Category = "Transfer Manager")
	FText GetCurrentInstructions() const;

	/** Get the state machine component */
	UFUNCTION(BlueprintCallable, Category = "Transfer Manager")
	UTransferStateMachine* GetStateMachine() const { return StateMachine; }

	/** Get the scoring component */
	UFUNCTION(BlueprintCallable, Category = "Transfer Manager")
	UScoringComponent* GetScoring() const { return Scoring; }

	/** Get the spine monitor component */
	UFUNCTION(BlueprintCallable, Category = "Transfer Manager")
	USpineMonitorComponent* GetSpineMonitor() const { return SpineMonitor; }

protected:
	// ============================================================
	// Components
	// ============================================================

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Transfer Manager")
	TObjectPtr<UTransferStateMachine> StateMachine;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Transfer Manager")
	TObjectPtr<UScoringComponent> Scoring;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Transfer Manager")
	TObjectPtr<USpineMonitorComponent> SpineMonitor;

	// ============================================================
	// Scene References (wire these in the editor details panel)
	// ============================================================

	/** The patient actor in the level */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Transfer Manager|Setup")
	TObjectPtr<APatientActor> PatientRef;

	/** The wheelchair actor in the level */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Transfer Manager|Setup")
	TObjectPtr<AWheelchairActor> WheelchairRef;

	/** The belt actor in the level */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Transfer Manager|Setup")
	TObjectPtr<ABeltActor> BeltRef;

	/** Whether to auto-start the transfer on BeginPlay */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transfer Manager|Setup")
	bool bAutoStart = false;

	/** Automatically adapt the existing CNA VR pawn for native patient/belt grabbing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transfer Manager|Setup")
	bool bAutoConfigureVRHands = true;

private:
	UPROPERTY(Transient)
	TObjectPtr<UVRPatientCareBridgeComponent> VRPatientCareBridge;
};
