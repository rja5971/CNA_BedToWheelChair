// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ScoringComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnScoreChanged, float, NewScore, float, MaxScore);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPenaltyApplied, FText, Reason);

/**
 * Scoring Component — tracks performance during patient transfer.
 * 
 * Single Responsibility: Only accumulates and reports score.
 * Attach to a game manager or the patient — it listens to events from other systems.
 */
UCLASS(ClassGroup = (PatientCare), meta = (BlueprintSpawnableComponent))
class HANDLINGRAGDOLLS_API UScoringComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UScoringComponent();

public:
	// ============================================================
	// Scoring Interface
	// ============================================================

	/** Add a penalty (reduces score) */
	UFUNCTION(BlueprintCallable, Category = "Scoring")
	void AddPenalty(float Amount, FText Reason);

	/** Add a reward (increases score) */
	UFUNCTION(BlueprintCallable, Category = "Scoring")
	void AddReward(float Amount, FText Reason);

	/** Get current score */
	UFUNCTION(BlueprintCallable, Category = "Scoring")
	float GetCurrentScore() const { return CurrentScore; }

	/** Get maximum possible score */
	UFUNCTION(BlueprintCallable, Category = "Scoring")
	float GetMaxScore() const { return MaxScore; }

	/** Get score as a percentage (0-100) */
	UFUNCTION(BlueprintCallable, Category = "Scoring")
	float GetScorePercent() const;

	/** Get the grade letter based on score percentage */
	UFUNCTION(BlueprintCallable, Category = "Scoring")
	FString GetGrade() const;

	/** Reset scoring to initial state */
	UFUNCTION(BlueprintCallable, Category = "Scoring")
	void ResetScore();

	/** Check if the simulation was passed (score above threshold) */
	UFUNCTION(BlueprintCallable, Category = "Scoring")
	bool HasPassed() const;

	/** Record time taken for the transfer */
	UFUNCTION(BlueprintCallable, Category = "Scoring")
	void SetCompletionTime(float TimeSeconds);

	/** Get time taken */
	UFUNCTION(BlueprintCallable, Category = "Scoring")
	float GetCompletionTime() const { return CompletionTime; }

	// ============================================================
	// Events
	// ============================================================

	UPROPERTY(BlueprintAssignable, Category = "Scoring|Events")
	FOnScoreChanged OnScoreChanged;

	UPROPERTY(BlueprintAssignable, Category = "Scoring|Events")
	FOnPenaltyApplied OnPenaltyApplied;

protected:
	/** Starting/maximum score */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scoring|Config")
	float MaxScore = 100.0f;

	/** Minimum passing score as percentage (0-100) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scoring|Config", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float PassingPercent = 70.0f;

	/** Penalty per second of spine in warning state */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scoring|Config")
	float SpineWarningPenaltyPerSecond = 2.0f;

	/** Penalty per second of spine in critical state */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scoring|Config")
	float SpineCriticalPenaltyPerSecond = 10.0f;

	/** Reward for completing step in proper order */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scoring|Config")
	float ProperStepReward = 10.0f;

private:
	float CurrentScore = 100.0f;
	float CompletionTime = 0.0f;
};
