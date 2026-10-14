// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TransferManagerActor.generated.h"

class APawn;
class UTransferStateMachine;
class UScoringComponent;
class USpineMonitorComponent;
class APatientActor;
class AWheelchairActor;
class ABeltActor;
class UVRPatientCareBridgeComponent;
class UTransferGuidanceComponent;
class UWidgetComponent;

UENUM(BlueprintType)
enum class EGuidedTrainingStage : uint8
{
    EntryQuiz, BathroomTransition, BathroomVideo, BathroomContinue, RoomTransition,
    Conversation, TransferVideo, PatientQuiz, Transfer, Complete, Error
};

/**
 * Transfer Manager Actor — the single orchestrator placed in the level.
 *
 * Owns the transfer state machine, scoring, and spine monitor components.
 * Wire the Patient, Wheelchair, and Belt references in the editor details panel,
 * then call StartTransfer() to begin the task.
 *
 * This keeps game logic off the individual actors (Single Responsibility) while
 * providing a central coordination point.
 */
UCLASS(BlueprintType)
class HANDLINGRAGDOLLS_API ATransferManagerActor : public AActor
{
	GENERATED_BODY()

public:
	ATransferManagerActor();

protected:
	virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CNA|Training")
    bool bGuidedTraining = false;
    UFUNCTION(BlueprintPure, Category="CNA|Training") EGuidedTrainingStage GetTrainingStage() const { return TrainingStage; }
    bool SubmitQuiz(UObject* Widget, int32 Answer);
    void DismissWrongAnswer();
    bool BeginRoomTransition();
    void FinishRoomTransition();
    bool BeginConversationResponse();
    bool BeginTransferLesson();
    bool CanRequestLesson(AActor* Display) const;
    void LessonCompleted(AActor* Display);
    bool IsTrainingMovementAllowed() const;
    bool IsTrainingLocationAllowed(const FVector& WorldLocation) const;
    FVector ClampTrainingMovementDelta(const APawn* Pawn, const FVector& Delta) const;
    /** Horizontal radius around arrival in each training area. Tracking stays free. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CNA|Training|Movement", meta=(ClampMin="0.1", Units="m"))
    float TrainingMovementRadiusMeters = 5.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CNA|Training|Movement", meta=(ClampMin="0", Units="cm/s"))
    float TrainingSmoothMoveSpeed = 150.f;
    /** World-space offset from the placed patient actor, in centimetres. Updates live in PIE. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CNA|Training|Popups", meta=(Units="cm", DisplayName="Transfer Instruction Offset"))
    FVector StatusPanelOffset = FVector(0.f, 0.f, 115.f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CNA|Training|Popups", meta=(Units="cm"))
    FVector WrongAnswerPopupOffset = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CNA|Training|Popups", meta=(Units="cm"))
    FVector ContinuePopupOffset = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CNA|Training|Popups", meta=(Units="cm"))
    FVector VideoStatusPopupOffset = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CNA|Training|Popups", meta=(ClampMin="0.01", DisplayName="Transfer Instruction Scale"))
    float StatusPanelScale = .06f;
    bool HandleInterruptedLift();
    void ScheduleLiftRetry();
    void SetTrainingStage(EGuidedTrainingStage Stage);

	/** Start the transfer task */
	UFUNCTION(BlueprintCallable, Category = "Transfer Manager")
	void StartTransfer();

	/** Reset the state machine and scoring */
	UFUNCTION(BlueprintCallable, Category = "Transfer Manager")
	void ResetTransfer();

	/** Get current instructions text (for HUD) */
	UFUNCTION(BlueprintCallable, Category = "Transfer Manager")
	FText GetCurrentInstructions() const;

	/** Get the state machine component */
	UFUNCTION(BlueprintCallable, Category = "Transfer Manager")
	UTransferStateMachine* GetStateMachine() const { return StateMachine; }

	/** Get the scoring component */
	UFUNCTION(BlueprintCallable, Category = "Transfer Manager")
	UScoringComponent* GetScoring() const { return Scoring; }
	UFUNCTION(BlueprintPure, Category="CNA|Hints")
	UTransferGuidanceComponent* GetTransferGuidance() const { return TransferGuidance; }

	/** Get the spine monitor component */
	UFUNCTION(BlueprintCallable, Category = "Transfer Manager")
	USpineMonitorComponent* GetSpineMonitor() const { return SpineMonitor; }

protected:
	// ============================================================
	// Components
	// ============================================================

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Transfer Manager")
	TObjectPtr<UTransferStateMachine> StateMachine;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Transfer Manager")
	TObjectPtr<UScoringComponent> Scoring;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Transfer Manager")
	TObjectPtr<USpineMonitorComponent> SpineMonitor;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CNA|Hints")
	TObjectPtr<UTransferGuidanceComponent> TransferGuidance;

	// ============================================================
	// Scene References (wire these in the editor details panel)
	// ============================================================

	/** The patient actor in the level */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Transfer Manager|Setup")
	TObjectPtr<APatientActor> PatientRef;

	/** The wheelchair actor in the level */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Transfer Manager|Setup")
	TObjectPtr<AWheelchairActor> WheelchairRef;

	/** The belt actor in the level */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Transfer Manager|Setup")
	TObjectPtr<ABeltActor> BeltRef;

	/** Whether to auto-start the transfer on BeginPlay */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transfer Manager|Setup")
	bool bAutoStart = false;

	/** Automatically adapt the existing CNA VR pawn for native patient/belt grabbing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transfer Manager|Setup")
	bool bAutoConfigureVRHands = true;

private:
    friend class FCNAGuidedTrainingTest;
    void InitializeTrainingUI();
    void UpdateTrainingUI();
    void UpdateStatus();
    void ApplyPopupOffset(UWidgetComponent* Panel, const FVector& Offset);
    TMap<TWeakObjectPtr<UWidgetComponent>, FVector> PopupBaseLocations;
    AActor* FindLesson(int32 ID) const;
    UFUNCTION() void TransferCompleted();
    UFUNCTION() void TransferFailed(FText Reason);
    EGuidedTrainingStage TrainingStage = EGuidedTrainingStage::EntryQuiz;
    bool bTrainingUIReady = false;
    bool bWrongAnswerOpen = false;
    bool bConversationResponded = false;
    bool bRetryMessage = false;
    bool bStatusAttempted = false;
    bool bWasMovementLocked = false;
    void CaptureMovementOrigin() const;
    mutable bool bMovementOriginValid = false;
    mutable FVector MovementOrigin = FVector::ZeroVector;
    FTimerHandle TrainingTransitionTimer;
    UPROPERTY(Transient) TObjectPtr<AActor> QuizActor;
    UPROPERTY(Transient) TObjectPtr<AActor> ChatActor;
    UPROPERTY(Transient) TObjectPtr<UWidgetComponent> TrainingStatus;
    FText TrainingError;
	UPROPERTY(Transient)
	TObjectPtr<UVRPatientCareBridgeComponent> VRPatientCareBridge;
};
