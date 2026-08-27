// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "../TransferTaskState.h"
#include "IdleState.generated.h"

/**
 * Idle State — waiting for the nurse to begin the task.
 * Automatically transitions when the task is started.
 */
UCLASS()
class HANDLINGRAGDOLLS_API UIdleState : public UTransferTaskState
{
	GENERATED_BODY()

public:
	virtual void EnterState(UTransferStateMachine* StateMachine) override;
	virtual bool CanTransitionToNext() const override;
	virtual ETransferState GetStateType() const override { return ETransferState::Idle; }
	virtual FText GetInstructions() const override;

private:
	bool bReadyToTransition = false;
};
