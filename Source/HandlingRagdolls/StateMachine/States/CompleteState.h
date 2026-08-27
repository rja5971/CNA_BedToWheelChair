// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "../TransferTaskState.h"
#include "CompleteState.generated.h"

/**
 * Complete State — the transfer task has been successfully completed.
 * Displays final score and results.
 */
UCLASS()
class HANDLINGRAGDOLLS_API UCompleteState : public UTransferTaskState
{
	GENERATED_BODY()

public:
	virtual void EnterState(UTransferStateMachine* StateMachine) override;
	virtual ETransferState GetStateType() const override { return ETransferState::Complete; }
	virtual FText GetInstructions() const override;
};
