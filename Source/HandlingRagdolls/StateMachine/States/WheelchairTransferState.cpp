// Fill out your copyright notice in the Description page of Project Settings.

#include "WheelchairTransferState.h"
#include "../TransferStateMachine.h"
#include "../../Interfaces/IPatient.h"
#include "../../Interfaces/ISpineMonitorable.h"
#include "../../Transfer/WheelchairActor.h"
#include "../../Components/BeltComponent.h"
#include "../../Components/ScoringComponent.h"
#include "../../Components/PatientPhysicsComponent.h"
#include "../../Components/SeatedTransitionComponent.h"
#include "../../Patient/PatientActor.h"
#include "EngineUtils.h"

void UWheelchairTransferState::EnterState(UTransferStateMachine* StateMachine)
{
	Super::EnterState(StateMachine);
	bPatientSeated = false;
	bSeatingTransitionStarted = false;
	ActiveWheelchair.Reset();
	CandidateWheelchair.Reset();
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
	UBeltComponent* Belt = OwningStateMachine->GetBelt();
	UScoringComponent* Scoring = OwningStateMachine->GetScoring();

	if (!Patient) return;

	if (bSeatingTransitionStarted)
	{
		AWheelchairActor* Wheelchair = ActiveWheelchair.Get();
		APatientActor* PatientActor = OwningStateMachine->GetPatient();
		USeatedTransitionComponent* Transition = PatientActor ? PatientActor->GetSeatedTransitionComponent() : nullptr;
				if (Wheelchair && Transition && Transition->IsSeatedLocked())
		{
			bPatientSeated = true;
			// CRITICAL: We must transition the patient to the Seated state! 
			// This permanently engages the pure animation driven flag, so that when 
			// the 0.2s carry release timer expires, it doesn't wake physics back up and ruin the anim!
			if (Patient)
			{
				Patient->SetPatientState(EPatientState::Seated);
			}
			Wheelchair->OnTransferComplete(PatientActor);
		}
		else if (!Transition || !Transition->IsSettling())
		{
			bSeatingTransitionStarted = false;
			if (Wheelchair)
			{
				Wheelchair->OnTransferFailed(PatientActor);
			}
			ActiveWheelchair.Reset();
		}
		return;
	}

	// Keep a ready chair latched while the pelvis remains in its generous approach
	// zone. This prevents duplicate/migrated chair actors from stealing selection.
	const FVector PelvisLocation = Patient->GetPelvisLocation();
	AWheelchairActor* Wheelchair = CandidateWheelchair.Get();
	if (!Wheelchair || !Wheelchair->IsReadyToReceive()
		|| !Wheelchair->IsLocationInApproachArea(PelvisLocation))
	{
		Wheelchair = nullptr;
		CandidateWheelchair.Reset();
		float BestScore = TNumericLimits<float>::Max();
		for (TActorIterator<AWheelchairActor> It(OwningStateMachine->GetWorld()); It; ++It)
		{
			AWheelchairActor* Candidate = *It;
			if (!Candidate->IsReadyToReceive()
				|| !Candidate->IsLocationInApproachArea(PelvisLocation))
			{
				continue;
			}

			const float Score = Candidate->GetSeatDistanceSquared(PelvisLocation);
			if (Score < BestScore)
			{
				BestScore = Score;
				Wheelchair = Candidate;
			}
		}

		if (Wheelchair)
		{
			CandidateWheelchair = Wheelchair;
			UE_LOG(LogTemp, Log, TEXT("WheelchairTransfer: Recognized ready chair %s."),
				*Wheelchair->GetName());
		}
	}

	bWheelchairReady = Wheelchair && Wheelchair->IsReadyToReceive();
	bPatientInRange = Wheelchair && Wheelchair->IsLocationInSeatArea(PelvisLocation);
	if (!Wheelchair) return;

	// --- Pivot Rotation: drive pelvis yaw from two-hand grab ---
	APatientActor* ConcretePatientForCarry = OwningStateMachine->GetPatient();
	if (Belt && Belt->IsTwoHandGrab()
		&& (!ConcretePatientForCarry || !ConcretePatientForCarry->IsKinematicCarryActive()))
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

	if (!bWheelchairReady)
	{
		// Penalize if trying to seat without locking brakes
		return;
	}

	// --- Check if patient pelvis is near the seat target ---
	FTransform SeatTransform = Wheelchair->GetTargetSeatTransform();
	FVector SeatLocation = SeatTransform.GetLocation();

	float Distance = FVector::Dist(PelvisLocation, SeatLocation);

	auto StartSeatingTransition = [&]()
	{
		AActor* PatientActor = Cast<AActor>(OwningStateMachine->GetPatientObject());
		Wheelchair->OnTransferBegin(PatientActor);

		APatientActor* ConcretePatient = OwningStateMachine->GetPatient();
		if (ConcretePatient && ConcretePatient->BeginSeatedTransitionAt(SeatTransform, Wheelchair))
		{
			ActiveWheelchair = Wheelchair;
			bSeatingTransitionStarted = true;
			UE_LOG(LogTemp, Log, TEXT("WheelchairTransfer: Starting seated animation on %s at %.1f cm."),
				*Wheelchair->GetName(), Distance);
		}
		else
		{
			Patient->SetPatientState(EPatientState::Seated);
			bPatientSeated = true;
			Wheelchair->OnTransferComplete(PatientActor);
		}
	};

	// Releasing the final belt handle in the seat zone is the authoritative
	// seating gesture. It bypasses the velocity gate and begins immediately.
	// Do not consume the one-shot release until a valid ready chair and commit-zone
	// match exist. A slightly early release therefore cannot be silently lost.
	const bool bFinalHandleReleased = Belt && Belt->HasPendingFinalHandleRelease();
	if (bFinalHandleReleased && bPatientInRange)
	{
		Belt->ConsumeFinalHandleRelease();
		UE_LOG(LogTemp, Log, TEXT("WheelchairTransfer: Final handle released inside %s seat area."),
			*Wheelchair->GetName());
		StartSeatingTransition();
		return;
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
	CandidateWheelchair.Reset();
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

	if (!CandidateWheelchair.IsValid())
	{
		return FText::FromString(TEXT("Guide the patient closer to the wheelchair seat area."));
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


