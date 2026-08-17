// Fill out your copyright notice in the Description page of Project Settings.

#include "TransferTaskState.h"
#include "TransferStateMachine.h"

void UTransferTaskState::EnterState(UTransferStateMachine* StateMachine)
{
	OwningStateMachine = StateMachine;
}

void UTransferTaskState::TickState(float DeltaTime)
{
	// Override in derived states
}

void UTransferTaskState::ExitState()
{
	// Override in derived states
}
