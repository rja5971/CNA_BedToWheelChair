// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TransferTaskState.h"
#include "../Interfaces/IPatient.h"
#include "TransferStateMachine.generated.h"

class APatientActor;
class AWheelchairActor;
class ABeltActor;
class UBeltComponent;
class UScoringComponent;
class USpineMonitorComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnTransferStateChanged, ETransferState, OldState, ETransferState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnTransferCompleted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTransferFailed, FText, Reason);

/**
 * Transfer State Machine — orchestrates the entire patient transfer procedure.
 * 
 * Dependency Inversion: Works with interfaces (ISpineMonitorable, ITransferTarget, etc.)
 * Open/Closed: New states can be added without modifying this class.
 * Single Responsibility: Only manages state transitions and task flow.
 * 
 * The state machine holds references to all participants but delegates behavior
 * to individual state objects.
 */
UCLASS(ClassGroup = (PatientCare), meta = (BlueprintSpawnableComponent))
class HANDLINGRAGDOLLS_API UTransferStateMachine : public UActorComponent
{
	GENERATED_BODY()

public:
	UTransferStateMachine();

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

public:
	// ============================================================
	// State Machine Control
	// ============================================================

	/** Start the transfer task */
	UFUNCTION(BlueprintCallable, Category = "Transfer State Machine")
	void StartTask();

	/** Programmatically wire all references (alternative to setting them in editor) */
	UFUNCTION(BlueprintCallable, Category = "Transfer State Machine")
	void Setup(TScriptInterface<IIPatient> InPatient, AWheelchairActor* InWheelchair, UBeltComponent* InBelt, UScoringComponent* InScoring);

	/** Force transition to a specific state (for debugging/reset) */
	UFUNCTION(BlueprintCallable, Category = "Transfer State Machine")
	void ForceState(ETransferState NewState);

	/** Reset the state machine to idle */
	UFUNCTION(BlueprintCallable, Category = "Transfer State Machine")
	void Reset();

	/** Get current state */
	UFUNCTION(BlueprintCallable, Category = "Transfer State Machine")
	ETransferState GetCurrentState() const { return CurrentStateType; }

	/** Get instructions for the current state */
	UFUNCTION(BlueprintCallable, Category = "Transfer State Machine")
	FText GetCurrentInstructions() const;

	/** Get elapsed time since task started */
	UFUNCTION(BlueprintCallable, Category = "Transfer State Machine")
	float GetElapsedTime() const { return ElapsedTime; }

	// ============================================================
	// Context Accessors (for states to query)
	// ============================================================

	APatientActor* GetPatient() const;
	IIPatient* GetPatientInterface() const;
	UObject* GetPatientObject() const;
	AWheelchairActor* GetWheelchair() const { return Wheelchair; }
	UBeltComponent* GetBelt() const;
	UScoringComponent* GetScoring() const { return Scoring; }

	// ============================================================
	// Events
	// ============================================================

	UPROPERTY(BlueprintAssignable, Category = "Transfer State Machine|Events")
	FOnTransferStateChanged OnTransferStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Transfer State Machine|Events")
	FOnTransferCompleted OnTransferCompleted;

	UPROPERTY(BlueprintAssignable, Category = "Transfer State Machine|Events")
	FOnTransferFailed OnTransferFailed;

protected:
	// ============================================================
	// References (set in editor or at runtime)
	// ============================================================

	/** The patient actor in the scene */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Transfer State Machine|Setup")
	TObjectPtr<APatientActor> PatientActor;

	/** The wheelchair in the scene */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Transfer State Machine|Setup")
	TObjectPtr<AWheelchairActor> Wheelchair;

	/** The belt actor in the scene (BeltComponent is resolved from this automatically) */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Transfer State Machine|Setup")
	TObjectPtr<ABeltActor> BeltActor;

	/** Scoring component (optional — leave empty if not using scoring) */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Transfer State Machine|Setup")
	TObjectPtr<UScoringComponent> Scoring;

	/** Ordered sequence of states for the transfer task. GetNextState() follows this order. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Transfer State Machine|Setup")
	TArray<ETransferState> StateSequence = {
		ETransferState::Idle,
		ETransferState::NeckSupport,
		ETransferState::BeltAttach,
		ETransferState::BeltLift,
		ETransferState::WheelchairTransfer,
		ETransferState::Complete
	};

private:
	/** All registered states */
	UPROPERTY()
	TMap<ETransferState, TObjectPtr<UTransferTaskState>> States;

	/** Currently active state object */
	UPROPERTY()
	TObjectPtr<UTransferTaskState> CurrentState;

	/** Current state type */
	ETransferState CurrentStateType = ETransferState::Idle;

	/** Time since task started */
	float ElapsedTime = 0.0f;

	/** Whether the task is running */
	bool bIsRunning = false;

	/** Transition to the next state */
	void TransitionToState(ETransferState NewState);

	/** Create and register all state objects */
	void InitializeStates();

	/** Get the next state in sequence */
	ETransferState GetNextState(ETransferState Current) const;
};
