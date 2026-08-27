// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "../TransferTaskState.h"
#include "BeltLiftState.generated.h"

/**
 * Belt Lift State — the nurse lifts the patient using the belt handles.
 * The patient must be lifted to a standing/upright enough position to be transferred.
 * Two-hand grip on the belt is preferred (better score).
 */
UCLASS()
class HANDLINGRAGDOLLS_API UBeltLiftState : public UTransferTaskState
{
	GENERATED_BODY()

public:
	virtual void EnterState(UTransferStateMachine* StateMachine) override;
	virtual void TickState(float DeltaTime) override;
	virtual bool CanTransitionToNext() const override;
	virtual bool HasFailed() const override;
	virtual ETransferState GetStateType() const override { return ETransferState::BeltLift; }
	virtual FText GetInstructions() const override;

private:
	/** Minimum height the patient pelvis must reach (above starting position, in cm) */
	float RequiredLiftHeight = 40.0f;

	/** Current height of the patient above start */
	float CurrentLiftHeight = 0.0f;

	/** Starting Z position of the patient pelvis */
	float StartingPelvisZ = 0.0f;

	/** Whether the lift requirement is met */
	bool bLiftComplete = false;

	/** Track if belt is being grabbed */
	bool bBeltIsBeingGrabbed = false;
};
