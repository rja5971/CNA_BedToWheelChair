// Fill out your copyright notice in the Description page of Project Settings.

#include "BeltLiftState.h"
#include "../TransferStateMachine.h"
#include "../../Interfaces/IPatient.h"
#include "../../Interfaces/ISpineMonitorable.h"
#include "../../Components/BeltComponent.h"
#include "../../Components/ScoringComponent.h"
#include "../../Transfer/WheelchairActor.h"
#include "EngineUtils.h"

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
		// Releasing inside any chair is an intentional seating handoff, even when
		// the vertical lift-height requirement was not reached.
		const FVector PelvisPos = Patient->GetPelvisLocation();
		for (TActorIterator<AWheelchairActor> It(OwningStateMachine->GetWorld()); It; ++It)
		{
			if (It->IsLocationInSeatArea(PelvisPos))
			{
				bLiftComplete = true;
				UE_LOG(LogTemp, Log, TEXT("BeltLift: Final handle released inside %s; advancing to seating."),
					*It->GetName());
				break;
			}
		}

		if (!bLiftComplete)
		{
			// Let go away from a chair before lift completion: re-anchor safely.
			Patient->SetPatientState(EPatientState::BeltAttached);
		}
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

		// Reaching any wheelchair is also a valid completion of the lift phase.
		// This prevents the workflow from remaining stuck in BeingLifted when the
		// nurse performs a shorter but otherwise valid bed-to-chair transfer.
		if (!bLiftComplete)
		{
			for (TActorIterator<AWheelchairActor> It(OwningStateMachine->GetWorld()); It; ++It)
			{
				AWheelchairActor* Chair = *It;
				const float SeatDistance = FVector::Dist(
					PelvisPos, Chair->GetTargetSeatTransform().GetLocation());
				if (SeatDistance <= Chair->GetAcceptanceRadius())
				{
					bLiftComplete = true;
					UE_LOG(LogTemp, Log, TEXT("BeltLift: Patient reached %s at %.1f cm; advancing to wheelchair transfer."),
						*Chair->GetName(), SeatDistance);
					break;
				}
			}
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
