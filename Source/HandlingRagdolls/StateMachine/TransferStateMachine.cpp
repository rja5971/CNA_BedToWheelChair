// Fill out your copyright notice in the Description page of Project Settings.

#include "TransferStateMachine.h"
#include "States/IdleState.h"
#include "States/NeckSupportState.h"
#include "States/BeltAttachState.h"
#include "States/BeltLiftState.h"
#include "States/WheelchairTransferState.h"
#include "States/CompleteState.h"
#include "../Components/ScoringComponent.h"
#include "../Components/BeltComponent.h"
#include "../Patient/PatientActor.h"
#include "../Transfer/WheelchairActor.h"
#include "../Transfer/BeltActor.h"

UTransferStateMachine::UTransferStateMachine()
{
	PrimaryComponentTick.bCanEverTick = true;
}

APatientActor* UTransferStateMachine::GetPatient() const
{
	return PatientActor;
}

IIPatient* UTransferStateMachine::GetPatientInterface() const
{
	return Cast<IIPatient>(PatientActor);
}

UObject* UTransferStateMachine::GetPatientObject() const
{
	return PatientActor;
}

UBeltComponent* UTransferStateMachine::GetBelt() const
{
	if (BeltActor)
	{
		return BeltActor->GetBeltComponent();
	}
	return nullptr;
}

void UTransferStateMachine::BeginPlay()
{
	Super::BeginPlay();
	InitializeStates();
}

void UTransferStateMachine::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bIsRunning || !CurrentState) return;

	ElapsedTime += DeltaTime;

	// Tick the current state
	CurrentState->TickState(DeltaTime);

	// Check for failure
	if (CurrentState->HasFailed())
	{
		TransitionToState(ETransferState::Failed);
		return;
	}

	// Check for transition
	if (CurrentState->CanTransitionToNext())
	{
		ETransferState NextState = GetNextState(CurrentStateType);
		TransitionToState(NextState);
	}
}

void UTransferStateMachine::StartTask()
{
	if (bIsRunning) return;

	// Validate that essential references are set
	if (!PatientActor)
	{
		UE_LOG(LogTemp, Error, TEXT("TransferStateMachine: Cannot start — no Patient reference set!"));
		return;
	}

	bIsRunning = true;
	ElapsedTime = 0.0f;

	// Start from Idle, which will immediately look for NeckSupport conditions
	TransitionToState(ETransferState::Idle);
}

void UTransferStateMachine::Setup(TScriptInterface<IIPatient> InPatient, AWheelchairActor* InWheelchair, UBeltComponent* InBelt, UScoringComponent* InScoring)
{
	PatientActor = Cast<APatientActor>(InPatient.GetObject());
	Wheelchair = InWheelchair;
	// BeltActor is resolved from the belt component's owner
	if (InBelt)
	{
		BeltActor = Cast<ABeltActor>(InBelt->GetOwner());
	}
	Scoring = InScoring;

	UE_LOG(LogTemp, Log, TEXT("TransferStateMachine: Setup complete. Patient=%s, Wheelchair=%s"),
		PatientActor ? *PatientActor->GetName() : TEXT("null"),
		Wheelchair ? *Wheelchair->GetName() : TEXT("null"));
}

void UTransferStateMachine::ForceState(ETransferState NewState)
{
	TransitionToState(NewState);
}

void UTransferStateMachine::Reset()
{
	if (CurrentState)
	{
		CurrentState->ExitState();
	}

	CurrentState = nullptr;
	CurrentStateType = ETransferState::Idle;
	ElapsedTime = 0.0f;
	bIsRunning = false;

	if (Scoring)
	{
		Scoring->ResetScore();
	}
}

FText UTransferStateMachine::GetCurrentInstructions() const
{
	if (CurrentState)
	{
		return CurrentState->GetInstructions();
	}
	return FText::FromString(TEXT("Press Start to begin the transfer task."));
}

void UTransferStateMachine::TransitionToState(ETransferState NewState)
{
	ETransferState OldState = CurrentStateType;

	// Exit current state
	if (CurrentState)
	{
		CurrentState->ExitState();
	}

	// Find and enter new state
	TObjectPtr<UTransferTaskState>* FoundState = States.Find(NewState);
	if (FoundState && *FoundState)
	{
		CurrentState = *FoundState;
		CurrentStateType = NewState;
		CurrentState->EnterState(this);
	}

	// Broadcast state change
	OnTransferStateChanged.Broadcast(OldState, NewState);

	// Handle terminal states
	if (NewState == ETransferState::Complete)
	{
		bIsRunning = false;
		if (Scoring)
		{
			Scoring->SetCompletionTime(ElapsedTime);
		}
		OnTransferCompleted.Broadcast();
	}
	else if (NewState == ETransferState::Failed)
	{
		bIsRunning = false;
		OnTransferFailed.Broadcast(FText::FromString(TEXT("Patient spine was injured during transfer.")));
	}

	// Reward for proper step completion
	if (Scoring && NewState != ETransferState::Failed && OldState != ETransferState::Idle)
	{
		Scoring->AddReward(10.0f, FText::FromString(TEXT("Step completed correctly")));
	}
}

void UTransferStateMachine::InitializeStates()
{
	// Register state objects for each entry in the StateSequence
	for (ETransferState StateType : StateSequence)
	{
		if (States.Contains(StateType)) continue;

		switch (StateType)
		{
		case ETransferState::Idle:
			States.Add(StateType, NewObject<UIdleState>(this));
			break;
		case ETransferState::NeckSupport:
			States.Add(StateType, NewObject<UNeckSupportState>(this));
			break;
		case ETransferState::BeltAttach:
			States.Add(StateType, NewObject<UBeltAttachState>(this));
			break;
		case ETransferState::BeltLift:
			States.Add(StateType, NewObject<UBeltLiftState>(this));
			break;
		case ETransferState::WheelchairTransfer:
			States.Add(StateType, NewObject<UWheelchairTransferState>(this));
			break;
		case ETransferState::Complete:
			States.Add(StateType, NewObject<UCompleteState>(this));
			break;
		default:
			break;
		}
	}
}

ETransferState UTransferStateMachine::GetNextState(ETransferState Current) const
{
	int32 CurrentIndex = StateSequence.IndexOfByKey(Current);
	if (CurrentIndex != INDEX_NONE && CurrentIndex + 1 < StateSequence.Num())
	{
		return StateSequence[CurrentIndex + 1];
	}
	return ETransferState::Complete;
}
