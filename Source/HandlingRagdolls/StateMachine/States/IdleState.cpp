// Fill out your copyright notice in the Description page of Project Settings.

#include "IdleState.h"
#include "../TransferStateMachine.h"

void UIdleState::EnterState(UTransferStateMachine* StateMachine)
{
	Super::EnterState(StateMachine);
	// Immediately ready to transition — task has been started
	bReadyToTransition = true;
}

bool UIdleState::CanTransitionToNext() const
{
	return bReadyToTransition;
}

FText UIdleState::GetInstructions() const
{
	return FText::FromString(TEXT("Approach the patient. Support their neck/head before anything else."));
}
