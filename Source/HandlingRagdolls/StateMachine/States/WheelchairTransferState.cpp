// Fill out your copyright notice in the Description page of Project Settings.

#include "WheelchairTransferState.h"
#include "../TransferStateMachine.h"
#include "../../Interfaces/IPatient.h"
#include "../../Interfaces/ISpineMonitorable.h"
#include "../../Transfer/WheelchairActor.h"
#include "../../Components/BeltComponent.h"
#include "../../Components/ScoringComponent.h"
#include "../../Components/PatientPhysicsComponent.h"
#include "../../Patient/PatientActor.h"

void UWheelchairTransferState::EnterState(UTransferStateMachine* StateMachine)
{
	Super::EnterState(StateMachine);
	bPatientSeated = false;
	bWheelchairReady = false;
	bPatientInRange = false;
	bPivotActive = false;

	// Transition patient to BeingTransferred state.
	// This triggers the state config with Pivot behavior on pelvis
	// (position locked, yaw free) + Anchored spine + Free legs.
	IIPatient* Patient = StateMachine->GetPatientInterface();
	if (Patient)
	{
		Patient->SetPatientState(EPatientState::BeingTransferred);
	}
}

void UWheelchairTransferState::TickState(float DeltaTime)
{
	Super::TickState(DeltaTime);

	if (!OwningStateMachine) return;

	IIPatient* Patient = OwningStateMachine->GetPatientInterface();
	AWheelchairActor* Wheelchair = OwningStateMachine->GetWheelchair();
	UBeltComponent* Belt = OwningStateMachine->GetBelt();
	UScoringComponent* Scoring = OwningStateMachine->GetScoring();

	if (!Patient || !Wheelchair) return;

	// --- Pivot Rotation: drive pelvis yaw from two-hand grab ---
	if (Belt && Belt->IsTwoHandGrab())
	{
		bPivotActive = true;

		// Get the yaw the hands are implying for the patient's facing direction
		float HandYaw = Belt->GetTwoHandYawDegrees();

		// Drive the patient's pelvis yaw
		APatientActor* PatientActor = OwningStateMachine->GetPatient();
		if (PatientActor)
		{
			UPatientPhysicsComponent* Physics = PatientActor->GetPatientPhysicsComponent();
			if (Physics && Physics->IsPivotActive())
			{
				Physics->SetPivotYaw(HandYaw);
			}
		}

		// Reward for proper two-hand technique
		if (Scoring)
		{
			Scoring->AddReward(0.3f * DeltaTime, FText::FromString(TEXT("Two-hand pivot rotation")));
		}
	}
	else
	{
		bPivotActive = false;
	}

	// --- Wheelchair readiness check ---
	bWheelchairReady = Wheelchair->IsReadyToReceive();

	if (!bWheelchairReady)
	{
		// Penalize if trying to seat without locking brakes
		return;
	}

	// --- Check if patient pelvis is near the seat target ---
	FTransform SeatTransform = Wheelchair->GetTargetSeatTransform();
	FVector SeatLocation = SeatTransform.GetLocation();

	FVector PelvisLocation = Patient->GetPelvisLocation();
	float Distance = FVector::Dist(PelvisLocation, SeatLocation);
	float AcceptRadius = Wheelchair->GetAcceptanceRadius();

	bPatientInRange = (Distance <= AcceptRadius);

	if (bPatientInRange)
	{
		// Check velocity — patient should be moving slowly when being seated
		FVector PelvisVelocity = Patient->GetPelvisVelocity();
		float Speed = PelvisVelocity.Size();

		if (Speed < 50.0f) // Low enough velocity = successful seating
		{
			bPatientSeated = true;
			AActor* PatientActor = Cast<AActor>(OwningStateMachine->GetPatientObject());
			Wheelchair->OnTransferBegin(PatientActor);
			Wheelchair->OnTransferComplete(PatientActor);

			// Set patient to seated state (exits pivot mode)
			Patient->SetPatientState(EPatientState::Seated);
		}
		else if (Speed > 100.0f && Scoring)
		{
			// Too fast! Penalize
			Scoring->AddPenalty(15.0f, FText::FromString(TEXT("Patient lowered too quickly into wheelchair")));
		}
	}

	// --- Continuous spine stress check ---
	if (Scoring)
	{
		float Stress = Patient->GetSpineStressLevel();
		if (Stress > 0.5f)
		{
			Scoring->AddPenalty(Stress * 5.0f * DeltaTime, FText::FromString(TEXT("Spine stress during transfer")));
		}
	}
}

void UWheelchairTransferState::ExitState()
{
	Super::ExitState();
	bPivotActive = false;
}

bool UWheelchairTransferState::CanTransitionToNext() const
{
	return bPatientSeated;
}

bool UWheelchairTransferState::HasFailed() const
{
	if (!OwningStateMachine) return false;

	IISpineMonitorable* SpineMonitor = Cast<IISpineMonitorable>(OwningStateMachine->GetPatientObject());
	if (!SpineMonitor) return false;

	return SpineMonitor->IsSpineDamaged();
}

FText UWheelchairTransferState::GetInstructions() const
{
	if (bPatientSeated)
	{
		return FText::FromString(TEXT("Excellent! Patient has been successfully seated in the wheelchair."));
	}

	if (!bWheelchairReady)
	{
		return FText::FromString(TEXT("Lock the wheelchair brakes first! Then guide the patient to the seat."));
	}

	if (!bPivotActive)
	{
		return FText::FromString(TEXT("Grab both belt handles to rotate the patient toward the wheelchair."));
	}

	if (bPatientInRange)
	{
		return FText::FromString(TEXT("Patient is over the seat. Lower them slowly and gently."));
	}

	return FText::FromString(TEXT("Rotate the patient to face away from the wheelchair, then lower them into the seat."));
}
