#include "TrainingMediaControllerComponent.h"
#include "CNAReliabilityLibrary.h"
#include "TrainingMediaErrorWidget.h"
#include "MediaPlayer.h"
#include "MediaSource.h"
#include "Components/WidgetComponent.h"
#include "Components/AudioComponent.h"
#include "Components/SceneComponent.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CoreDelegates.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogCNAMedia, Log, All);
#if !UE_BUILD_SHIPPING
static TAutoConsoleVariable<int32> ForceOpenFailure(TEXT("cna.Media.ForceOpenFailure"), 0, TEXT("Force video open failure for recovery testing."));
static TAutoConsoleVariable<int32> ForceStall(TEXT("cna.Media.ForceStall"), 0, TEXT("Ignore video progress for recovery testing."));
static FAutoConsoleCommandWithWorld SmokeOpenFailure(TEXT("cna.Media.SmokeOpenFailure"),
    TEXT("Exercise real request, automatic retry, and error-widget construction, then exit with a test status."),
    FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
    {
        if (!World || !World->IsGameWorld()) return;
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            UObject* Content = UCNAReliabilityLibrary::ObjectProperty(*It, TEXT("ContentData"));
            if (!Content || !Content->GetName().Contains(TEXT("Basin"))) continue;
            AActor* Display = *It;
            ForceOpenFailure.AsVariable()->Set(1, ECVF_SetByConsole);
            UCNAReliabilityLibrary::CancelPlayback(Display);
            UCNAReliabilityLibrary::RequestPlayback(Display);
            FTimerHandle Check;
            World->GetTimerManager().SetTimer(Check, [World, Display]()
            {
                UTrainingMediaControllerComponent* Controller = nullptr;
                for (TActorIterator<AActor> Actor(World); Actor; ++Actor)
                    if (UTrainingMediaControllerComponent* Found = Actor->FindComponentByClass<UTrainingMediaControllerComponent>()) Controller = Found;
                UWidgetComponent* Text = Cast<UWidgetComponent>(UCNAReliabilityLibrary::ObjectProperty(Display, TEXT("TextWidget")));
                UTrainingMediaErrorWidget* Error = Text ? Cast<UTrainingMediaErrorWidget>(Text->GetUserWidgetObject()) : nullptr;
                UButton* Retry = nullptr;
                if (Error && Error->WidgetTree) Error->WidgetTree->ForEachWidget([&Retry](UWidget* Widget) { if (UButton* Button = Cast<UButton>(Widget)) Retry = Button; });
                bool bPassed = Controller && Controller->GetPlaybackState() == FTrainingPlaybackSession::EState::Failed && Retry;
                if (bPassed)
                {
                    // Exercise the actual error widget's bound click delegate.
                    Retry->OnClicked.Broadcast();
                    bPassed = Controller->GetPlaybackState() == FTrainingPlaybackSession::EState::RetryPending;
                    UCNAReliabilityLibrary::RequestPlayback(Display); // Duplicate request must preserve retry state.
                    bPassed &= Controller->GetPlaybackState() == FTrainingPlaybackSession::EState::RetryPending;
                }
                FTimerHandle VerifyRetry;
                World->GetTimerManager().SetTimer(VerifyRetry, [Controller, Text, Display, bPassed]()
                {
                    bool bResult = bPassed && Controller && Controller->GetPlaybackState() == FTrainingPlaybackSession::EState::Failed;
                    UCNAReliabilityLibrary::CancelPlayback(Display);
                    bResult &= Controller && Controller->GetPlaybackState() == FTrainingPlaybackSession::EState::Idle;
                    bResult &= Text && !Cast<UTrainingMediaErrorWidget>(Text->GetUserWidgetObject());
                    UE_LOG(LogCNAMedia, Display, TEXT("Runtime startup-failure, Retry-button, duplicate-request and cancellation smoke test: %s"), bResult ? TEXT("PASS") : TEXT("FAIL"));
                    ForceOpenFailure.AsVariable()->Set(0, ECVF_SetByConsole);
                    FPlatformMisc::RequestExitWithStatus(false, bResult ? 0 : 1);
                }, 1.5f, false);
            }, 1.5f, false);
            return;
        }
        UE_LOG(LogCNAMedia, Error, TEXT("Runtime smoke test could not find the bathroom display"));
        FPlatformMisc::RequestExitWithStatus(false, 1);
    }));

#endif

UTrainingMediaControllerComponent::UTrainingMediaControllerComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UTrainingMediaControllerComponent::RequestPlayback(AActor* Display)
{
    if (!IsValid(Display) || bEndingPlay || CompletedDisplays.Contains(Display)) return;
    if (ActiveDisplay == Display && Session.State != FTrainingPlaybackSession::EState::Idle) return;
    if (ActiveDisplay) CancelPlayback(ActiveDisplay);
    ActiveDisplay = Display;
    UObject* Content = UCNAReliabilityLibrary::ObjectProperty(Display, TEXT("ContentData"));
    Source = Cast<UMediaSource>(UCNAReliabilityLibrary::ObjectProperty(Content, TEXT("MediaSource")));
    Player = Cast<UMediaPlayer>(UCNAReliabilityLibrary::ObjectProperty(Display, TEXT("MediaPlayer")));
    if (!BackgroundHandle.IsValid())
    {
        BackgroundHandle = FCoreDelegates::ApplicationWillEnterBackgroundDelegate.AddUObject(this, &UTrainingMediaControllerComponent::EnterBackground);
        ForegroundHandle = FCoreDelegates::ApplicationHasEnteredForegroundDelegate.AddUObject(this, &UTrainingMediaControllerComponent::EnterForeground);
    }
    UE_LOG(LogCNAMedia, Display, TEXT("Request display=%s source=%s"), *GetNameSafe(Display), *GetPathNameSafe(Source));
    BeginAttempt(true);
}

void UTrainingMediaControllerComponent::BeginAttempt(bool bNewRequest)
{
    if (!IsValid(ActiveDisplay) || bEndingPlay) return;
    RestoreErrorWidget();
    Session.Start(GetWorld()->GetRealTimeSeconds(), bNewRequest);
    SetPresentation(true);
    SetStatusText(NSLOCTEXT("CNAMedia", "Loading", "Loading video..."));
    GetWorld()->GetTimerManager().SetTimer(WatchdogTimer, this, &UTrainingMediaControllerComponent::Watchdog, 0.5f, true);
    if (!Player || !Source) { Fail(TEXT("Missing media player or content media source")); return; }
    // Close while in Opening, before binding, so an old end/closed event cannot complete training.
    UnbindPlayer();
    Player->Close();
    Player->PlayOnOpen = false;
    Player->OnMediaOpened.AddDynamic(this, &UTrainingMediaControllerComponent::MediaOpened);
    Player->OnMediaOpenFailed.AddDynamic(this, &UTrainingMediaControllerComponent::MediaOpenFailed);
    Player->OnEndReached.AddDynamic(this, &UTrainingMediaControllerComponent::MediaEnded);
    ExpectedUrl = Source->GetUrl();
#if !UE_BUILD_SHIPPING
    if (ForceOpenFailure.GetValueOnGameThread()) { Fail(TEXT("Injected open failure")); return; }
#endif
    UE_LOG(LogCNAMedia, Display, TEXT("Opening url=%s retry=%d"), *ExpectedUrl, Session.AutomaticRetries);
    if (!Player->OpenSource(Source)) Fail(TEXT("OpenSource rejected the request"));
}

void UTrainingMediaControllerComponent::MediaOpened(FString Url)
{
    if (!Player || Url != ExpectedUrl || !IsValid(ActiveDisplay) || !Session.Opened(GetWorld()->GetRealTimeSeconds())) return;
    if (!Player->Play()) { Fail(TEXT("Play rejected after media opened")); return; }
    UObject* Content = UCNAReliabilityLibrary::ObjectProperty(ActiveDisplay, TEXT("ContentData"));
    if (FTextProperty* Text = FindFProperty<FTextProperty>(Content ? Content->GetClass() : nullptr, TEXT("DisplayText")))
        SetStatusText(Text->GetPropertyValue_InContainer(Content));
    if (UAudioComponent* Audio = Cast<UAudioComponent>(UCNAReliabilityLibrary::ObjectProperty(ActiveDisplay, TEXT("AudioPlayer"))))
    {
        if (USoundBase* Sound = Cast<USoundBase>(UCNAReliabilityLibrary::ObjectProperty(Content, TEXT("AudioCue"))))
        { Audio->SetSound(Sound); Audio->Play(); }
    }
    UE_LOG(LogCNAMedia, Display, TEXT("Playing display=%s url=%s"), *GetNameSafe(ActiveDisplay), *Url);
}

void UTrainingMediaControllerComponent::MediaOpenFailed(FString Url)
{
    if (Url == ExpectedUrl) Fail(TEXT("OnMediaOpenFailed: ") + Url);
}

void UTrainingMediaControllerComponent::Watchdog()
{
    if (!IsValid(ActiveDisplay)) { CancelPlayback(ActiveDisplay); return; }
    if (bApplicationSuspended) return;
    const double Now = GetWorld()->GetRealTimeSeconds();
    bool bObserveProgress = true;
#if !UE_BUILD_SHIPPING
    bObserveProgress = ForceStall.GetValueOnGameThread() == 0;
#endif
    if (Player && bObserveProgress) Session.Progress(Player->GetTime().GetTotalSeconds(), Now);
    if (Player && Player->HasError()) Fail(TEXT("Media player reported an error"));
    else if (Session.TimedOut(Now)) Fail(Session.State == FTrainingPlaybackSession::EState::Opening ? TEXT("Video startup timed out") : TEXT("Video stopped advancing"));
}

void UTrainingMediaControllerComponent::Fail(const FString& Reason)
{
    if (!Session.Fail()) return;
    UE_LOG(LogCNAMedia, Warning, TEXT("Failure display=%s reason=%s next=%s"), *GetNameSafe(ActiveDisplay), *Reason,
        Session.State == FTrainingPlaybackSession::EState::RetryPending ? TEXT("automatic retry") : TEXT("manual retry"));
    UnbindPlayer();
    if (Player) Player->Close();
    if (UAudioComponent* Audio = Cast<UAudioComponent>(UCNAReliabilityLibrary::ObjectProperty(ActiveDisplay, TEXT("AudioPlayer")))) Audio->Stop();
    if (Session.State == FTrainingPlaybackSession::EState::RetryPending)
    {
        // Defer reopening until the old player's close operation and queued callbacks have drained.
        GetWorld()->GetTimerManager().SetTimer(RetryTimer, [this]() { BeginAttempt(false); }, 0.5f, false);
    }
    else
    {
        GetWorld()->GetTimerManager().ClearTimer(WatchdogTimer);
        ShowError();
    }
}

void UTrainingMediaControllerComponent::MediaEnded()
{
    if (Player) Session.Progress(Player->GetTime().GetTotalSeconds(), GetWorld()->GetRealTimeSeconds());
    if (!Session.Complete() || !IsValid(ActiveDisplay)) return;
    AActor* Completed = ActiveDisplay;
    CompletedDisplays.Add(Completed);
    GetWorld()->GetTimerManager().ClearTimer(WatchdogTimer);
    GetWorld()->GetTimerManager().ClearTimer(RetryTimer);
    UnbindPlayer();
    if (Player) Player->Close();
    RestoreErrorWidget();
    SetPresentation(false);
    ActiveDisplay = nullptr;
    Source = nullptr;
    UE_LOG(LogCNAMedia, Display, TEXT("Completed display=%s (training completion dispatched once)"), *GetNameSafe(Completed));
    UCNAReliabilityLibrary::CallNoArgs(Completed, TEXT("NotifyPlaybackComplete"));
}

void UTrainingMediaControllerComponent::CancelPlayback(AActor* Display)
{
    if (Display && Display != ActiveDisplay) return;
    Session.Cancel();
    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(WatchdogTimer);
        GetWorld()->GetTimerManager().ClearTimer(RetryTimer);
    }
    UnbindPlayer();
    if (Player) Player->Close();
    RestoreErrorWidget();
    SetPresentation(false);
    UE_LOG(LogCNAMedia, Display, TEXT("Canceled display=%s; no training completion"), *GetNameSafe(ActiveDisplay));
    ActiveDisplay = nullptr;
    Source = nullptr;
}

void UTrainingMediaControllerComponent::RetryPlayback()
{
    if (Session.State == FTrainingPlaybackSession::EState::Failed && IsValid(ActiveDisplay))
    {
        UE_LOG(LogCNAMedia, Display, TEXT("Manual retry display=%s"), *GetNameSafe(ActiveDisplay));
        BeginAttempt(true);
    }
}

void UTrainingMediaControllerComponent::SetPresentation(bool bVisible)
{
    if (!IsValid(ActiveDisplay)) return;
    for (FName Name : { FName(TEXT("ScreenMesh")), FName(TEXT("TextWidget")) })
        if (USceneComponent* Component = Cast<USceneComponent>(UCNAReliabilityLibrary::ObjectProperty(ActiveDisplay, Name))) Component->SetVisibility(bVisible);
    if (!bVisible)
        if (UAudioComponent* Audio = Cast<UAudioComponent>(UCNAReliabilityLibrary::ObjectProperty(ActiveDisplay, TEXT("AudioPlayer")))) Audio->Stop();
}

void UTrainingMediaControllerComponent::SetStatusText(const FText& Text)
{
    UWidgetComponent* Component = Cast<UWidgetComponent>(UCNAReliabilityLibrary::ObjectProperty(ActiveDisplay, TEXT("TextWidget")));
    if (!Component) return;
    Component->InitWidget();
    UObject* Widget = Component->GetUserWidgetObject();
    UFunction* Function = Widget ? Widget->FindFunction(TEXT("SetDisplayText")) : nullptr;
    if (!Function) return;
    FStructOnScope Params(Function);
    if (FTextProperty* Property = FindFProperty<FTextProperty>(Function, TEXT("InText")))
        Property->SetPropertyValue_InContainer(Params.GetStructMemory(), Text);
    Widget->ProcessEvent(Function, Params.GetStructMemory());
}

void UTrainingMediaControllerComponent::ShowError()
{
    ErrorComponent = Cast<UWidgetComponent>(UCNAReliabilityLibrary::ObjectProperty(ActiveDisplay, TEXT("TextWidget")));
    if (!ErrorComponent) { UE_LOG(LogCNAMedia, Error, TEXT("Missing TextWidget; use the VR menu to restart")); return; }
    ErrorComponent->InitWidget();
    OriginalWidget = ErrorComponent->GetUserWidgetObject();
    OriginalDrawSize = ErrorComponent->GetDrawSize();
    UTrainingMediaErrorWidget* Error = CreateWidget<UTrainingMediaErrorWidget>(GetWorld(), UTrainingMediaErrorWidget::StaticClass());
    if (!Error) { UE_LOG(LogCNAMedia, Error, TEXT("Could not construct playback retry widget; use Restart")); return; }
    Error->SetController(this);
    ErrorComponent->SetWidget(Error);
    ErrorComponent->SetDrawSize(FVector2D(1000, 320));
    ErrorComponent->SetVisibility(true);
}

void UTrainingMediaControllerComponent::RestoreErrorWidget()
{
    if (IsValid(ErrorComponent))
    {
        ErrorComponent->SetWidget(OriginalWidget);
        ErrorComponent->SetDrawSize(OriginalDrawSize);
    }
    ErrorComponent = nullptr;
    OriginalWidget = nullptr;
}

void UTrainingMediaControllerComponent::UnbindPlayer()
{
    if (!Player) return;
    Player->OnMediaOpened.RemoveDynamic(this, &UTrainingMediaControllerComponent::MediaOpened);
    Player->OnMediaOpenFailed.RemoveDynamic(this, &UTrainingMediaControllerComponent::MediaOpenFailed);
    Player->OnEndReached.RemoveDynamic(this, &UTrainingMediaControllerComponent::MediaEnded);
}

void UTrainingMediaControllerComponent::EnterBackground()
{
    bApplicationSuspended = true;
    if (Player && Session.State == FTrainingPlaybackSession::EState::Playing) Player->Pause();
}

void UTrainingMediaControllerComponent::EnterForeground()
{
    bApplicationSuspended = false;
    if (Session.State == FTrainingPlaybackSession::EState::Playing)
    {
        Session.LastProgressAt = GetWorld()->GetRealTimeSeconds();
        if (Player && !Player->Play()) Fail(TEXT("Could not resume after returning to the app"));
    }
    else if (Session.State == FTrainingPlaybackSession::EState::Opening)
        Session.StartedAt = GetWorld()->GetRealTimeSeconds();
}

void UTrainingMediaControllerComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    bEndingPlay = true;
    CancelPlayback(ActiveDisplay);
    FCoreDelegates::ApplicationWillEnterBackgroundDelegate.Remove(BackgroundHandle);
    FCoreDelegates::ApplicationHasEnteredForegroundDelegate.Remove(ForegroundHandle);
    Super::EndPlay(Reason);
}
