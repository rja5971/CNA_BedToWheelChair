// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "../TransferTaskState.h"
#include "WheelchairTransferState.generated.h"

class AWheelchairActor;

/**
 * Wheelchair Transfer State — guide the lifted patient to the wheelchair and seat them.
 * 
 * On entry, sets the patient to BeingTransferred state (which activates Pivot behavior
 * on the pelvis). When both belt handles are grabbed, the patient's pelvis yaw is driven
 * by the nurse's hand positions — allowing the nurse to rotate the patient in place to
 * face the wheelchair, then lower them into the seat.
 * 
 * The wheelchair must have brakes locked. Patient must be lowered slowly into the seat.
 */
UCLASS()
class HANDLINGRAGDOLLS_API UWheelchairTransferState : public UTransferTaskState
{
	GENERATED_BODY()

public:
	virtual void EnterState(UTransferStateMachine* StateMachine) override;
	virtual void TickState(float DeltaTime) override;
	virtual void ExitState() override;
	virtual bool CanTransitionToNext() const override;
	virtual bool HasFailed() const override;
	virtual ETransferState GetStateType() const override { return ETransferState::WheelchairTransfer; }
	virtual FText GetInstructions() const override;

private:
	/** Whether the patient has been successfully seated */
	bool bPatientSeated = false;

	/** Whether the asynchronous physics-to-animation handoff has started. */
	bool bSeatingTransitionStarted = false;

	/** The actual nearby wheelchair selected for the current handoff. */
	TWeakObjectPtr<AWheelchairActor> ActiveWheelchair;

	/** Ready chair latched while the patient's pelvis remains in its approach zone. */
	TWeakObjectPtr<AWheelchairActor> CandidateWheelchair;

	/** Whether the wheelchair is ready */
	bool bWheelchairReady = false;

	/** Whether the patient is within range of the wheelchair */
	bool bPatientInRange = false;

	/** Whether pivot rotation is currently active (two-hand grab) */
	bool bPivotActive = false;
};
