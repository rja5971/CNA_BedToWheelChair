// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "../TransferTaskState.h"
#include "BeltAttachState.generated.h"

/**
 * Belt Attach State — the nurse must attach the transfer belt around the patient's torso.
 * The belt must be properly positioned and secured before lifting can begin.
 */
UCLASS()
class HANDLINGRAGDOLLS_API UBeltAttachState : public UTransferTaskState
{
	GENERATED_BODY()

public:
	virtual void EnterState(UTransferStateMachine* StateMachine) override;
	virtual void TickState(float DeltaTime) override;
	virtual bool CanTransitionToNext() const override;
	virtual bool HasFailed() const override;
	virtual ETransferState GetStateType() const override { return ETransferState::BeltAttach; }
	virtual FText GetInstructions() const override;

private:
	bool bBeltAttached = false;
};
