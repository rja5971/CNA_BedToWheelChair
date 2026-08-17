// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "TransferTaskState.generated.h"

class UTransferStateMachine;

/**
 * Enumeration of all transfer task states.
 */
UENUM(BlueprintType)
enum class ETransferState : uint8
{
	Idle				UMETA(DisplayName = "Idle"),
	NeckSupport			UMETA(DisplayName = "Neck Support"),
	BeltAttach			UMETA(DisplayName = "Belt Attach"),
	BeltLift			UMETA(DisplayName = "Belt Lift"),
	WheelchairTransfer	UMETA(DisplayName = "Wheelchair Transfer"),
	Complete			UMETA(DisplayName = "Complete"),
	Failed				UMETA(DisplayName = "Failed")
};

/**
 * Base class for all transfer task states.
 * 
 * Open/Closed: New states can be added by deriving from this class.
 * Each state is responsible for its own entry/exit logic and transition conditions.
 */
UCLASS(Abstract, BlueprintType)
class HANDLINGRAGDOLLS_API UTransferTaskState : public UObject
{
	GENERATED_BODY()

public:
	/** Called when entering this state */
	virtual void EnterState(UTransferStateMachine* StateMachine);

	/** Called every frame while in this state */
	virtual void TickState(float DeltaTime);

	/** Called when exiting this state */
	virtual void ExitState();

	/** Check if conditions are met to transition to the next state */
	virtual bool CanTransitionToNext() const { return false; }

	/** Check if the state has failed (should transition to Failed state) */
	virtual bool HasFailed() const { return false; }

	/** Get the state enum this class represents */
	virtual ETransferState GetStateType() const { return ETransferState::Idle; }

	/** Get human-readable instructions for the player */
	virtual FText GetInstructions() const { return FText::GetEmpty(); }

protected:
	/** Reference to the owning state machine */
	UPROPERTY()
	TObjectPtr<UTransferStateMachine> OwningStateMachine;
};
