// Fill out your copyright notice in the Description page of Project Settings.

#include "CompleteState.h"
#include "../TransferStateMachine.h"
#include "../../Components/ScoringComponent.h"

void UCompleteState::EnterState(UTransferStateMachine* StateMachine)
{
	Super::EnterState(StateMachine);

	UE_LOG(LogTemp, Log, TEXT("Transfer Task Complete!"));

	UScoringComponent* Scoring = StateMachine->GetScoring();
	if (Scoring)
	{
		Scoring->SetCompletionTime(StateMachine->GetElapsedTime());
		UE_LOG(LogTemp, Log, TEXT("Final Score: %.1f / %.1f (%s)"),
			Scoring->GetCurrentScore(),
			Scoring->GetMaxScore(),
			*Scoring->GetGrade());
	}
}

FText UCompleteState::GetInstructions() const
{
	if (!OwningStateMachine) return FText::FromString(TEXT("Task Complete!"));

	UScoringComponent* Scoring = OwningStateMachine->GetScoring();
	if (Scoring)
	{
		const FString Result = FString::Printf(
			TEXT("Transfer Complete!\nScore: %.0f%%\nGrade: %s\nTime taken: %.0f seconds\nResult: %s"),
			Scoring->GetScorePercent(),
			*Scoring->GetGrade(),
			Scoring->GetCompletionTime(),
			Scoring->HasPassed() ? TEXT("Passed") : TEXT("Failed - Score below threshold")
		);

		return FText::FromString(Result);
	}

	return FText::FromString(TEXT("Transfer Complete!"));
}
