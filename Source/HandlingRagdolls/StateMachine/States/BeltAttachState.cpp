// Fill out your copyright notice in the Description page of Project Settings.

#include "BeltAttachState.h"
#include "../TransferStateMachine.h"
#include "../../Interfaces/IPatient.h"
#include "../../Interfaces/ISpineMonitorable.h"
#include "../../Components/BeltComponent.h"
#include "../../Components/ScoringComponent.h"

void UBeltAttachState::EnterState(UTransferStateMachine* StateMachine)
{
	Super::EnterState(StateMachine);
	bBeltAttached = false;
}

void UBeltAttachState::TickState(float DeltaTime)
{
	Super::TickState(DeltaTime);

	if (!OwningStateMachine) return;

	IIPatient* Patient = OwningStateMachine->GetPatientInterface();
	if (!Patient) return;

	// Check if belt has been attached
	if (Patient->HasBeltAttached())
	{
		bBeltAttached = true;
	}

	// Penalize if neck support is lost during belt attachment
	if (!Patient->IsNeckSupported())
	{
		UScoringComponent* Scoring = OwningStateMachine->GetScoring();
		if (Scoring)
		{
			Scoring->AddPenalty(5.0f * DeltaTime, FText::FromString(TEXT("Neck support lost during belt attachment")));
		}
	}
}

bool UBeltAttachState::CanTransitionToNext() const
{
	return bBeltAttached;
}

bool UBeltAttachState::HasFailed() const
{
	if (!OwningStateMachine) return false;

	IISpineMonitorable* SpineMonitor = Cast<IISpineMonitorable>(OwningStateMachine->GetPatientObject());
	if (!SpineMonitor) return false;

	return SpineMonitor->IsSpineDamaged();
}

FText UBeltAttachState::GetInstructions() const
{
	if (bBeltAttached)
	{
		return FText::FromString(TEXT("Belt attached! Now grab the belt handles and carefully lift the patient."));
	}
	return FText::FromString(TEXT("Attach the transfer belt around the patient's torso. Keep supporting the neck!"));
}
