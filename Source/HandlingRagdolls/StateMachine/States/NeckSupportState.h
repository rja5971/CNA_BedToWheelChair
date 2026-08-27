// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "../TransferTaskState.h"
#include "NeckSupportState.generated.h"

/**
 * Neck Support State — the nurse must grab and support the patient's neck/head.
 * This is the first critical step: you never move a spine-injured patient
 * without first stabilizing the cervical spine.
 */
UCLASS()
class HANDLINGRAGDOLLS_API UNeckSupportState : public UTransferTaskState
{
	GENERATED_BODY()

public:
	virtual void EnterState(UTransferStateMachine* StateMachine) override;
	virtual void TickState(float DeltaTime) override;
	virtual bool CanTransitionToNext() const override;
	virtual bool HasFailed() const override;
	virtual ETransferState GetStateType() const override { return ETransferState::NeckSupport; }
	virtual FText GetInstructions() const override;

private:
	/** How long the neck must be supported before we allow next step (seconds) */
	float RequiredSupportDuration = 2.0f;

	/** How long the neck has been continuously supported */
	float CurrentSupportTime = 0.0f;

	/** Whether the support requirement has been met */
	bool bSupportRequirementMet = false;
};
