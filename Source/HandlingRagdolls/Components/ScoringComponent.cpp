// Fill out your copyright notice in the Description page of Project Settings.

#include "ScoringComponent.h"

UScoringComponent::UScoringComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	CurrentScore = MaxScore;
}

void UScoringComponent::AddPenalty(float Amount, FText Reason)
{
	if (Amount <= 0.0f) return;

	CurrentScore = FMath::Max(0.0f, CurrentScore - Amount);
	OnScoreChanged.Broadcast(CurrentScore, MaxScore);
	OnPenaltyApplied.Broadcast(Reason);
}

void UScoringComponent::AddReward(float Amount, FText Reason)
{
	if (Amount <= 0.0f) return;

	CurrentScore = FMath::Min(MaxScore, CurrentScore + Amount);
	OnScoreChanged.Broadcast(CurrentScore, MaxScore);
}

float UScoringComponent::GetScorePercent() const
{
	if (MaxScore <= 0.0f) return 0.0f;
	return (CurrentScore / MaxScore) * 100.0f;
}

FString UScoringComponent::GetGrade() const
{
	float Percent = GetScorePercent();

	if (Percent >= 90.0f) return TEXT("A");
	if (Percent >= 80.0f) return TEXT("B");
	if (Percent >= 70.0f) return TEXT("C");
	if (Percent >= 60.0f) return TEXT("D");
	return TEXT("F");
}

void UScoringComponent::ResetScore()
{
	CurrentScore = MaxScore;
	CompletionTime = 0.0f;
	OnScoreChanged.Broadcast(CurrentScore, MaxScore);
}

bool UScoringComponent::HasPassed() const
{
	return GetScorePercent() >= PassingPercent;
}

void UScoringComponent::SetCompletionTime(float TimeSeconds)
{
	CompletionTime = TimeSeconds;
}
