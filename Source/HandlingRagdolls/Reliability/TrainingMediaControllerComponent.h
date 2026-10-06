#pragma once

#include "Components/ActorComponent.h"
#include "TrainingPlaybackSession.h"
#include "TrainingMediaControllerComponent.generated.h"

class UMediaPlayer;
class UMediaSource;
class UWidgetComponent;
class UUserWidget;

/** The only owner of MP_Display. Blueprint displays request playback through this component. */
UCLASS(ClassGroup=(CNA), meta=(BlueprintSpawnableComponent))
class HANDLINGRAGDOLLS_API UTrainingMediaControllerComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UTrainingMediaControllerComponent();
	UFUNCTION(BlueprintCallable, Category="CNA|Media") void RequestPlayback(AActor* Display);
	UFUNCTION(BlueprintCallable, Category="CNA|Media") void CancelPlayback(AActor* Display);
	UFUNCTION(BlueprintCallable, Category="CNA|Media") void RetryPlayback();
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	FTrainingPlaybackSession::EState GetPlaybackState() const { return Session.State; }

private:
	UPROPERTY() TObjectPtr<UMediaPlayer> Player;
	UPROPERTY() TObjectPtr<UMediaSource> Source;
	UPROPERTY() TObjectPtr<AActor> ActiveDisplay;
	UPROPERTY() TObjectPtr<UWidgetComponent> ErrorComponent;
	UPROPERTY() TObjectPtr<UUserWidget> OriginalWidget;
	FVector2D OriginalDrawSize;
	TSet<TWeakObjectPtr<AActor>> CompletedDisplays;
	FTrainingPlaybackSession Session;
	FTimerHandle WatchdogTimer;
	FTimerHandle RetryTimer;
	FString ExpectedUrl;
	bool bEndingPlay = false;
	bool bApplicationSuspended = false;
	FDelegateHandle BackgroundHandle;
	FDelegateHandle ForegroundHandle;
	void BeginAttempt(bool bNewRequest);
	void Watchdog();
	void Fail(const FString& Reason);
	void SetPresentation(bool bVisible);
	void SetStatusText(const FText& Text);
	void ShowError();
	void RestoreErrorWidget();
	void UnbindPlayer();
	UFUNCTION() void MediaOpened(FString Url);
	UFUNCTION() void MediaOpenFailed(FString Url);
	UFUNCTION() void MediaEnded();
	void EnterBackground();
	void EnterForeground();
};
