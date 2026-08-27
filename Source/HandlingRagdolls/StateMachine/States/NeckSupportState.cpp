// Fill out your copyright notice in the Description page of Project Settings.

#include "NeckSupportState.h"
#include "../TransferStateMachine.h"
#include "../../Interfaces/IPatient.h"
#include "../../Interfaces/ISpineMonitorable.h"

void UNeckSupportState::EnterState(UTransferStateMachine* StateMachine)
{
	Super::EnterState(StateMachine);
	CurrentSupportTime = 0.0f;
	bSupportRequirementMet = false;
}

void UNeckSupportState::TickState(float DeltaTime)
{
	Super::TickState(DeltaTime);

	if (!OwningStateMachine) return;

	IIPatient* Patient = OwningStateMachine->GetPatientInterface();
	if (!Patient) return;

	// Reaching the bed-seated/belt phase proves that neck support was performed.
	// The bed handoff can release the physics grab before RequiredSupportDuration,
	// so do not leave the task state machine stranded behind the actual workflow.
	const EPatientState PatientState = Patient->GetPatientState();
	if (Patient->HasBeltAttached()
		|| PatientState == EPatientState::Seated
		|| PatientState == EPatientState::BeltAttached
		|| PatientState == EPatientState::BeingLifted
		|| PatientState == EPatientState::BeingTransferred)
	{
		bSupportRequirementMet = true;
		return;
	}

	// Check if neck is being supported
	if (Patient->IsNeckSupported())
	{
		CurrentSupportTime += DeltaTime;

		if (CurrentSupportTime >= RequiredSupportDuration)
		{
			bSupportRequirementMet = true;
		}
	}
	else
	{
		// Reset if neck support is lost
		CurrentSupportTime = 0.0f;
	}
}

bool UNeckSupportState::CanTransitionToNext() const
{
	return bSupportRequirementMet;
}

bool UNeckSupportState::HasFailed() const
{
	if (!OwningStateMachine) return false;

	IISpineMonitorable* SpineMonitor = Cast<IISpineMonitorable>(OwningStateMachine->GetPatientObject());
	if (SpineMonitor && SpineMonitor->IsSpineDamaged())
	{
		return true;
	}

	return false;
}

FText UNeckSupportState::GetInstructions() const
{
	if (bSupportRequirementMet)
	{
		return FText::FromString(TEXT("Good! Neck is supported. Now attach the transfer belt around the patient's torso."));
	}
	return FText::FromString(TEXT("Gently grab and support the patient's neck/head. Hold steady for 2 seconds."));
}
