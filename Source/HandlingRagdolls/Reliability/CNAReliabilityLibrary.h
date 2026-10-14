#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "CNAReliabilityLibrary.generated.h"

class APawn;
class UWidgetComponent;

/** Small Blueprint adapters retaining the existing binary assets' entry points. */
UCLASS()
class HANDLINGRAGDOLLS_API UCNAReliabilityLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category="CNA|Recovery")
	static void RequestPlayback(AActor* Display);
	UFUNCTION(BlueprintCallable, Category="CNA|Recovery")
	static void CancelPlayback(AActor* Display);
	UFUNCTION(BlueprintCallable, Category="CNA|Recovery", meta=(WorldContext="WorldContextObject"))
	static void RetryPlayback(const UObject* WorldContextObject);
	UFUNCTION(BlueprintCallable, Category="CNA|Recovery", meta=(WorldContext="WorldContextObject"))
	static void RestartTraining(const UObject* WorldContextObject);
	UFUNCTION(BlueprintCallable, Category="CNA|Recovery", meta=(WorldContext="WorldContextObject"))
	static void ResetOrientation(const UObject* WorldContextObject);
	UFUNCTION(BlueprintCallable, Category="CNA|Recovery", meta=(WorldContext="WorldContextObject"))
	static void ExitTraining(const UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category="CNA|UI")
	static void StopMenuInteraction(AActor* Menu);
	UFUNCTION(BlueprintPure, Category="CNA|Patient UI")
	static bool IsPatientConversationEnabled(AActor* ChatActor);
	UFUNCTION(BlueprintCallable, Category="CNA|Patient UI")
	static void ApplyPatientConversationPolicy(AActor* ChatActor);

	/** Runtime-only inspection helpers. Retain the existing quiz button delegates. */
	UFUNCTION(BlueprintCallable, Category="CNA|UI Testing")
	static bool SetPreviewQuizQuestion(UObject* QuizWidget, int32 QuestionIndex);
	UFUNCTION(BlueprintCallable, Category="CNA|UI Testing", meta=(WorldContext="WorldContextObject"))
	static UWidgetComponent* CreateBeltUIPreview(const UObject* WorldContextObject);

    UFUNCTION(BlueprintCallable, Category="CNA|Training") static void RequestGuidedConversationPanel(AActor* ChatActor);
    UFUNCTION(BlueprintCallable, Category="CNA|Training") static void SubmitGuidedQuiz(UObject* Widget, int32 Answer);
    UFUNCTION(BlueprintCallable, Category="CNA|Training", meta=(WorldContext="WorldContextObject")) static void DismissGuidedWrongAnswer(const UObject* WorldContextObject);
    UFUNCTION(BlueprintCallable, Category="CNA|Training", meta=(WorldContext="WorldContextObject")) static bool BeginGuidedRoomTransition(const UObject* WorldContextObject);
    UFUNCTION(BlueprintCallable, Category="CNA|Training", meta=(WorldContext="WorldContextObject")) static void FinishGuidedRoomTransition(const UObject* WorldContextObject);
    UFUNCTION(BlueprintCallable, Category="CNA|Training", meta=(WorldContext="WorldContextObject")) static bool BeginGuidedConversationResponse(const UObject* WorldContextObject);
    UFUNCTION(BlueprintCallable, Category="CNA|Training", meta=(WorldContext="WorldContextObject")) static bool BeginGuidedTransferLesson(const UObject* WorldContextObject);
    UFUNCTION(BlueprintPure, Category="CNA|Training", meta=(WorldContext="WorldContextObject")) static bool IsTrainingMovementAllowed(const UObject* WorldContextObject);
    UFUNCTION(BlueprintCallable, Category="CNA|Training", meta=(WorldContext="WorldContextObject")) static void TrainingFadeOut(const UObject* WorldContextObject);
    UFUNCTION(BlueprintCallable, Category="CNA|Training", meta=(WorldContext="WorldContextObject")) static void TrainingFadeIn(const UObject* WorldContextObject);
    UFUNCTION(BlueprintPure, Category="CNA|Training", meta=(WorldContext="WorldContextObject"))
    static bool ValidateTrainingTeleportLocation(const UObject* WorldContextObject, bool bNavigationValid, FVector ProjectedLocation);
    UFUNCTION(BlueprintPure, Category="CNA|Training", meta=(WorldContext="WorldContextObject"))
    static bool IsPendingTrainingTeleportAllowed(const UObject* WorldContextObject);
    UFUNCTION(BlueprintCallable, Category="CNA|Training")
    static void AddTrainingMovementInput(APawn* Pawn, FVector WorldDirection, float ScaleValue = 1.f, bool bForce = false);
    static FVector TrainingViewLocation(const APawn* Pawn);
    static void EnsureTrainingFade(AActor* Pawn);
    static class ATransferManagerActor* GuidedManager(const UObject* Context);

	static UObject* ObjectProperty(const UObject* Object, FName Name);
	static void CallNoArgs(UObject* Object, FName Function);
};
