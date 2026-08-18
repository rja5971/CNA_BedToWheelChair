// Fill out your copyright notice in the Description page of Project Settings.

#include "BeltLiftState.h"
#include "../TransferStateMachine.h"
#include "../../Interfaces/IPatient.h"
#include "../../Interfaces/ISpineMonitorable.h"
#include "../../Components/BeltComponent.h"
#include "../../Components/ScoringComponent.h"

void UBeltLiftState::EnterState(UTransferStateMachine* StateMachine)
{
	Super::EnterState(StateMachine);

	bLiftComplete = false;
	bBeltIsBeingGrabbed = false;
	CurrentLiftHeight = 0.0f;

	// Don't un-anchor the pelvis immediately! Wait until they actually grab the belt.
	// Otherwise they instantly collapse when the belt attaches.
}

void UBeltLiftState::TickState(float DeltaTime)
{
	Super::TickState(DeltaTime);

	if (!OwningStateMachine) return;

	IIPatient* Patient = OwningStateMachine->GetPatientInterface();
	UBeltComponent* Belt = OwningStateMachine->GetBelt();
	UScoringComponent* Scoring = OwningStateMachine->GetScoring();

	if (!Patient || !Belt) return;

	bool bWasBeingGrabbed = bBeltIsBeingGrabbed;
	// Check if belt is being used to lift
	bBeltIsBeingGrabbed = Belt->IsBeingLifted();

	if (bBeltIsBeingGrabbed && !bWasBeingGrabbed)
	{
		// Just grabbed! Free the pelvis.
		Patient->SetPatientState(EPatientState::BeingLifted);
		StartingPelvisZ = Patient->GetPelvisLocation().Z;
	}
	else if (!bBeltIsBeingGrabbed && bWasBeingGrabbed && !bLiftComplete)
	{
		// Let go before lift complete! Re-anchor them (e.g. BeltAttached).
		Patient->SetPatientState(EPatientState::BeltAttached);
	}

	if (bBeltIsBeingGrabbed)
	{
		// Track pelvis height
		FVector PelvisPos = Patient->GetPelvisLocation();
		CurrentLiftHeight = PelvisPos.Z - StartingPelvisZ;

		if (CurrentLiftHeight >= RequiredLiftHeight)
		{
			bLiftComplete = true;
		}

		// Bonus for two-hand lift (proper technique)
		if (Belt->GetActiveGrabCount() >= 2 && Scoring)
		{
			// Small continuous reward for proper technique
			Scoring->AddReward(0.5f * DeltaTime, FText::FromString(TEXT("Two-hand belt grip")));
		}
	}

	// Penalize if lifting without belt (grabbing patient directly while belt is available)
	if (!bBeltIsBeingGrabbed && Patient->HasBeltAttached() && Scoring)
	{
		// Check if someone is grabbing the patient directly
		// This is handled implicitly — the patient shouldn't be grabbed directly in this phase
	}

	// Penalize rapid movements (spine stress)
	if (Scoring)
	{
		float Stress = Patient->GetSpineStressLevel();
		if (Stress > 0.5f)
		{
			Scoring->AddPenalty(Stress * 5.0f * DeltaTime, FText::FromString(TEXT("Spine stress during lift")));
		}
	}
}

bool UBeltLiftState::CanTransitionToNext() const
{
	return bLiftComplete;
}

bool UBeltLiftState::HasFailed() const
{
	if (!OwningStateMachine) return false;

	IISpineMonitorable* SpineMonitor = Cast<IISpineMonitorable>(OwningStateMachine->GetPatientObject());
	if (!SpineMonitor) return false;

	return SpineMonitor->IsSpineDamaged();
}

FText UBeltLiftState::GetInstructions() const
{
	if (bLiftComplete)
	{
		return FText::FromString(TEXT("Patient is upright! Now carefully guide them to the wheelchair."));
	}

	if (!bBeltIsBeingGrabbed)
	{
		return FText::FromString(TEXT("Grab the belt handles with both hands and lift the patient slowly."));
	}

	FString Progress = FString::Printf(TEXT("Lifting... %.0f%% complete. Lift slowly and steadily."),
		FMath::Clamp(CurrentLiftHeight / RequiredLiftHeight * 100.0f, 0.0f, 100.0f));
	return FText::FromString(Progress);
}
