#include "TransferManagerActor.h"
#include "../Reliability/CNAReliabilityLibrary.h"
#include "../Patient/PatientActor.h"
#include "../Components/PatientCarryComponent.h"
#include "../Components/PatientCinematicComponent.h"
#include "../Components/BeltComponent.h"
#include "../StateMachine/TransferStateMachine.h"
#include "BeltActor.h"
#include "Components/WidgetComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace
{
int32 Integer(const UObject* Object, FName Name, int32 Default = -1)
{
    const FIntProperty* Field = IsValid(Object) ? FindFProperty<FIntProperty>(Object->GetClass(), Name) : nullptr;
    return Field ? Field->GetPropertyValue_InContainer(Object) : Default;
}
UWidgetComponent* Panel(AActor* Actor, FName Name)
{
    return Cast<UWidgetComponent>(UCNAReliabilityLibrary::ObjectProperty(Actor, Name));
}
void Present(UWidgetComponent* Widget, bool bVisible)
{
    if (!Widget) return;
    Widget->InitWidget();
    Widget->SetHiddenInGame(!bVisible);
    Widget->SetVisibility(bVisible);
    Widget->SetCollisionEnabled(bVisible ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
    if (UUserWidget* Instance = Widget->GetUserWidgetObject())
    {
        Instance->SetIsEnabled(bVisible);
        Instance->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    }
}
}

void ATransferManagerActor::InitializeTrainingUI()
{
    if (bTrainingUIReady) return;
    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
    {
        const FName Class = It->GetClass()->GetFName();
        if (Class == TEXT("BP_QuizActor_C")) QuizActor = *It;
        if (Class == TEXT("BP_PatientChat_C")) ChatActor = *It;
    }
    if (!QuizActor || !ChatActor) return;
    bTrainingUIReady = true;
    UpdateTrainingUI();
}

void ATransferManagerActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bGuidedTraining) return;
    InitializeTrainingUI();
    UpdateTrainingUI();
    UpdateStatus();
    if (bTrainingUIReady && !bInitialLessonPrepared)
    {
        bInitialLessonPrepared = true;
        bLessonArrivalPending = true;
        PrepareLessonArrival();
    }
    // Visibility and tracked movement never start a transition. Arrivals are
    // requested by stage entry, popup dismissal or an explicit media recovery.
    UpdateWalkHologram();
    const bool bLocked = !IsTrainingMovementAllowed();
    if (!bLocked) CaptureMovementOrigin();
    if (bLocked && !bWasMovementLocked)
    {
        if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0))
        {
            if (FBoolProperty* Trace = FindFProperty<FBoolProperty>(Pawn->GetClass(), TEXT("bTeleportTraceActive")))
                Trace->SetPropertyValue_InContainer(Pawn, false);
            if (FBoolProperty* Valid = FindFProperty<FBoolProperty>(Pawn->GetClass(), TEXT("bValidTeleportLocation")))
                Valid->SetPropertyValue_InContainer(Pawn, false);
            UCNAReliabilityLibrary::CallNoArgs(Pawn, TEXT("EndTeleportTrace"));
        }
    }
    bWasMovementLocked = bLocked;
}

void ATransferManagerActor::EndPlay(const EEndPlayReason::Type Reason)
{
    GetWorldTimerManager().ClearTimer(TrainingTransitionTimer);
    GetWorldTimerManager().ClearTimer(LessonArrivalTimer);
    Super::EndPlay(Reason);
}

void ATransferManagerActor::SetTrainingStage(EGuidedTrainingStage Stage)
{
    if (TrainingStage == Stage || TrainingStage == EGuidedTrainingStage::Complete) return;
    const EGuidedTrainingStage Previous = TrainingStage;
    TrainingStage = Stage;
    if (Stage == EGuidedTrainingStage::EntryQuiz || Stage == EGuidedTrainingStage::PatientQuiz
        || Stage == EGuidedTrainingStage::TransferVideo || Stage == EGuidedTrainingStage::BathroomTransition
        || (Stage == EGuidedTrainingStage::BathroomVideo && Previous != EGuidedTrainingStage::BathroomTransition))
        bLessonArrivalPending = true;
    bMovementOriginValid = false;
    if (bLessonArrivalPending) PrepareLessonArrival();
    UE_LOG(LogTemp, Display, TEXT("GuidedTraining: stage=%d"), int32(Stage));
    UpdateTrainingUI();
}

void ATransferManagerActor::UpdateTrainingUI()
{
    const bool bTransfer = TrainingStage == EGuidedTrainingStage::Transfer;
    if (PatientRef)
        PatientRef->SetTrainingPermissions(bTransfer,
            bTransfer && PatientRef->GetInteractionPhase() == EPatientInteractionPhase::BeltTransfer);
    if (!bTrainingUIReady) return;
    const bool bQuiz = TrainingStage == EGuidedTrainingStage::EntryQuiz || TrainingStage == EGuidedTrainingStage::PatientQuiz;
    Present(Panel(QuizActor, TEXT("QuizPanel")), bQuiz && !bWrongAnswerOpen && !bLessonRelocating);
    Present(Panel(QuizActor, TEXT("PopupComponent")), bQuiz && bWrongAnswerOpen);
    ApplyPopupOffset(Panel(QuizActor, TEXT("PopupComponent")), WrongAnswerPopupOffset);
    const bool bChat = TrainingStage == EGuidedTrainingStage::Conversation;
    ChatActor->SetActorHiddenInGame(!bChat);
    Present(Panel(ChatActor, TEXT("ChatPanel")), bChat);
    // Obsolete lift/seating buttons cannot invoke the hidden legacy patient.
    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
    {
        if (It->GetClass()->GetFName() == TEXT("BP_MediaDisplay_C"))
            ApplyPopupOffset(Panel(*It, TEXT("TextWidget")), VideoStatusPopupOffset);
        if (It->GetClass()->GetFName() == TEXT("BP_PopupActor_C"))
        {
            // The legacy actor contains both a replacement popup and a Continue widget.
            TArray<UWidgetComponent*> Widgets; It->GetComponents(Widgets);
            for (UWidgetComponent* Widget : Widgets) ApplyPopupOffset(Widget, ContinuePopupOffset);
        }
        if (It->GetClass()->GetFName() != TEXT("BP_StepTrigger_C")) continue;
        It->SetActorEnableCollision(false);
        Present(Panel(*It, TEXT("Widget")), false);
    }
}

void ATransferManagerActor::ApplyPopupOffset(UWidgetComponent* Widget, const FVector& Offset)
{
    if (!IsValid(Widget)) return;
    const TWeakObjectPtr<UWidgetComponent> Key(Widget);
    if (!PopupBaseLocations.Contains(Key)) PopupBaseLocations.Add(Key, Widget->GetRelativeLocation());
    Widget->SetRelativeLocation(PopupBaseLocations.FindChecked(Key) + Offset);
}

bool ATransferManagerActor::SubmitQuiz(UObject* Widget, int32 Answer)
{
    InitializeTrainingUI();
    UWidgetComponent* Quiz = Panel(QuizActor, TEXT("QuizPanel"));
    const int32 Expected = TrainingStage == EGuidedTrainingStage::EntryQuiz ? 0
        : TrainingStage == EGuidedTrainingStage::PatientQuiz ? 1 : -1;
    if (!Quiz || Quiz->GetUserWidgetObject() != Widget || Expected < 0 || bWrongAnswerOpen || bLessonRelocating
        || Integer(Widget, TEXT("CurrentQuestionIndex")) != Expected || Answer < 1 || Answer > 3) return false;
    const int32 Correct = Integer(Widget, TEXT("CorrectAnswer"));
    if (Correct < 1 || Correct > 3) return false;
    if (Answer != Correct)
    {
        bWrongAnswerOpen = true;
        UpdateTrainingUI();
        return false;
    }
    if (!PatientRef || !BeltRef || !FindLesson(Expected == 0 ? 1 : 2))
    {
        TrainingError = FText::FromString(TEXT("Required training actors are missing. Training cannot advance safely."));
        SetTrainingStage(EGuidedTrainingStage::Error);
        return false;
    }
    // Commit before invoking any Blueprint or media callback.
    SetTrainingStage(Expected == 0 ? EGuidedTrainingStage::BathroomTransition : EGuidedTrainingStage::Transfer);
    if (FIntProperty* Index = FindFProperty<FIntProperty>(Widget->GetClass(), TEXT("CurrentQuestionIndex")))
        Index->SetPropertyValue_InContainer(Widget, Expected + 1);
    if (Expected == 0)
    {
        UCNAReliabilityLibrary::CallNoArgs(Widget, TEXT("TeleportPlayerToNextTarget"));
        GetWorldTimerManager().SetTimer(TrainingTransitionTimer, [this]()
        {
            if (TrainingStage != EGuidedTrainingStage::BathroomTransition) return;
            SetTrainingStage(EGuidedTrainingStage::BathroomVideo);
            if (!bLessonRelocating) UCNAReliabilityLibrary::RequestPlayback(FindLesson(1));
        }, 1.5f, false);
    }
    else StartTransfer();
    return true;
}

void ATransferManagerActor::DismissWrongAnswer()
{
    if (!bWrongAnswerOpen) return;
    bWrongAnswerOpen = false;
    PrepareLessonArrival();
    UpdateTrainingUI();
}

bool ATransferManagerActor::BeginRoomTransition()
{
    if (TrainingStage != EGuidedTrainingStage::BathroomContinue) return false;
    SetTrainingStage(EGuidedTrainingStage::RoomTransition);
    return true;
}

void ATransferManagerActor::FinishRoomTransition()
{
    if (TrainingStage != EGuidedTrainingStage::RoomTransition) return;
    GetWorldTimerManager().SetTimer(TrainingTransitionTimer, [this]()
    {
        if (TrainingStage == EGuidedTrainingStage::RoomTransition) SetTrainingStage(EGuidedTrainingStage::Conversation);
    }, .5f, false);
}

bool ATransferManagerActor::BeginConversationResponse()
{
    if (TrainingStage != EGuidedTrainingStage::Conversation || bConversationResponded) return false;
    bConversationResponded = true;
    return true;
}

bool ATransferManagerActor::BeginTransferLesson()
{
    if (TrainingStage != EGuidedTrainingStage::Conversation || !bConversationResponded) return false;
    AActor* Lesson = FindLesson(2);
    if (!Lesson) return false;
    UWidgetComponent* Quiz = Panel(QuizActor, TEXT("QuizPanel"));
    UObject* Widget = Quiz ? Quiz->GetUserWidgetObject() : nullptr;
    for (const TPair<FName, FName>& Names : { TPair<FName, FName>(TEXT("TargetVector"), TEXT("TargetLocation")), TPair<FName, FName>(TEXT("TargetRotator"), TEXT("TargetRotation")) })
    {
        FStructProperty* Source = FindFProperty<FStructProperty>(ChatActor->GetClass(), Names.Key);
        FStructProperty* Target = Widget ? FindFProperty<FStructProperty>(Widget->GetClass(), Names.Value) : nullptr;
        if (Source && Target && Source->Struct == Target->Struct)
            Target->CopyCompleteValue(Target->ContainerPtrToValuePtr<void>(Widget), Source->ContainerPtrToValuePtr<void>(ChatActor));
    }
    SetTrainingStage(EGuidedTrainingStage::TransferVideo);
    if (!bLessonRelocating) UCNAReliabilityLibrary::RequestPlayback(Lesson);
    return true;
}

AActor* ATransferManagerActor::FindLesson(int32 ID) const
{
    const FString ContentName = ID == 1 ? TEXT("DA_Basin_Content") : TEXT("DA_BenToWheelchairVideo");
    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
    {
        if (It->GetClass()->GetFName() != TEXT("BP_MediaDisplay_C")) continue;
        UObject* Content = UCNAReliabilityLibrary::ObjectProperty(*It, TEXT("ContentData"));
        if (Content && Content->GetFName() == FName(*ContentName)) return *It;
    }
    return nullptr;
}

bool ATransferManagerActor::CanRequestLesson(AActor* Display) const
{
    if (bLessonRelocating) return false;
    return (TrainingStage == EGuidedTrainingStage::BathroomVideo && Display == FindLesson(1))
        || (TrainingStage == EGuidedTrainingStage::TransferVideo && Display == FindLesson(2));
}

void ATransferManagerActor::LessonPlaybackRecovered(AActor* Display)
{
    if (bGuidedTraining && CanRequestLesson(Display)) PrepareLessonArrival();
}

void ATransferManagerActor::LessonCompleted(AActor* Display)
{
    if (!CanRequestLesson(Display)) return;
    if (TrainingStage == EGuidedTrainingStage::BathroomVideo) SetTrainingStage(EGuidedTrainingStage::BathroomContinue);
    else
    {
        // Move the next lesson before its arrival fade, preserving the authored quiz scale.
        if (QuizActor && Display) QuizActor->SetActorLocationAndRotation(Display->GetActorLocation(), Display->GetActorQuat());
        UWidgetComponent* Quiz = Panel(QuizActor, TEXT("QuizPanel"));
        if (Quiz) UCNAReliabilityLibrary::CallNoArgs(Quiz->GetUserWidgetObject(), TEXT("LoadNextQuestion"));
        SetTrainingStage(EGuidedTrainingStage::PatientQuiz);
    }
}

bool ATransferManagerActor::IsTrainingMovementAllowed() const
{
    if (!bGuidedTraining) return true;
    if (bLessonRelocating) return false;
    if (IsValid(UCNAReliabilityLibrary::ObjectProperty(UGameplayStatics::GetPlayerPawn(this, 0), TEXT("MenuReference")))) return false;
    if (TrainingStage == EGuidedTrainingStage::BathroomTransition || TrainingStage == EGuidedTrainingStage::RoomTransition
        || TrainingStage == EGuidedTrainingStage::Error) return false;
    return TrainingStage != EGuidedTrainingStage::Transfer || !PatientRef
        || PatientRef->GetInteractionPhase() != EPatientInteractionPhase::WheelchairSeating;
}

void ATransferManagerActor::CaptureMovementOrigin() const
{
    bMovementOriginValid = GetLessonMovementCenter(MovementOrigin);
}

bool ATransferManagerActor::IsTrainingLocationAllowed(const FVector& WorldLocation) const
{
    if (!bGuidedTraining) return true;
    if (!IsTrainingMovementAllowed() || WorldLocation.ContainsNaN()) return false;
    if (!IsLessonBoundaryActive()) return true;
    CaptureMovementOrigin();
    return bMovementOriginValid && FVector::DistSquared2D(WorldLocation, MovementOrigin)
        <= FMath::Square(FMath::Max(.1f, TrainingMovementRadiusMeters) * 100.f) + KINDA_SMALL_NUMBER;
}

FVector ATransferManagerActor::ClampTrainingMovementDelta(const APawn* Pawn, const FVector& Delta) const
{
    if (!bGuidedTraining) return Delta;
    if (!Pawn || !IsTrainingMovementAllowed() || Delta.ContainsNaN()) return FVector::ZeroVector;
    if (!IsLessonBoundaryActive()) return FVector(Delta.X, Delta.Y, 0.f);
    CaptureMovementOrigin();
    if (!bMovementOriginValid) return FVector::ZeroVector;
    const FVector Current = UCNAReliabilityLibrary::TrainingViewLocation(Pawn);
    FVector Result(Delta.X, Delta.Y, 0.f);
    const FVector Proposed = Current + Result;
    const float Radius = FMath::Max(.1f, TrainingMovementRadiusMeters) * 100.f;
    if (FVector::DistSquared2D(Proposed, MovementOrigin) <= FMath::Square(Radius)) return Result;
    // Physical tracking is never pulled back into the virtual boundary. Permit an inward control step.
    if (FVector::DistSquared2D(Current, MovementOrigin) > FMath::Square(Radius))
        return FVector::DistSquared2D(Proposed, MovementOrigin) < FVector::DistSquared2D(Current, MovementOrigin)
            ? Result : FVector::ZeroVector;
    FVector Offset = Proposed - MovementOrigin;
    Offset.Z = 0.f;
    const FVector Clamped = MovementOrigin + Offset.GetSafeNormal() * Radius;
    return FVector(Clamped.X - Current.X, Clamped.Y - Current.Y, 0.f);
}

bool ATransferManagerActor::HandleInterruptedLift()
{
    if (!bGuidedTraining) return false;
    // Never let a late recovery callback undo an accepted chair handoff or terminal state.
    if (TrainingStage != EGuidedTrainingStage::Transfer || !PatientRef
        || PatientRef->GetInteractionPhase() != EPatientInteractionPhase::BeltTransfer) return true;
    UBeltComponent* Belt = BeltRef ? BeltRef->GetBeltComponent() : nullptr;
    if (!Belt || Belt->GetActiveGrabCount() != 0) return true;
    if (!PatientRef->RestoreBedCheckpoint() || !StateMachine->RetryBeltLift())
    {
        TrainingError = FText::FromString(TEXT("Cannot restore a safe seated checkpoint. Stop this attempt and use the recovery menu."));
        SetTrainingStage(EGuidedTrainingStage::Error);
        return true;
    }
    bRetryMessage = true;
    UpdateTrainingUI();
    UpdateStatus();
    return true;
}

void ATransferManagerActor::TransferCompleted()
{
    if (TrainingStage != EGuidedTrainingStage::Transfer) return;
    SetTrainingStage(EGuidedTrainingStage::Complete);
    UpdateStatus();
}
void ATransferManagerActor::TransferFailed(FText Reason)
{
    if (TrainingStage == EGuidedTrainingStage::Complete) return;
    UE_LOG(LogTemp, Warning, TEXT("GuidedTraining: %s"), *Reason.ToString());
    TrainingError = Reason;
    SetTrainingStage(EGuidedTrainingStage::Error);
    UpdateStatus();
}

FVector ATransferManagerActor::CarryStatusLocation(const APawn* Pawn) const
{
    const FVector Pelvis = PatientRef ? PatientRef->GetPelvisLocation() : GetActorLocation();
    FVector Forward = Pawn ? Pelvis - UCNAReliabilityLibrary::TrainingViewLocation(Pawn) : FVector::ForwardVector;
    Forward.Z = 0.f;
    if (!Forward.Normalize()) Forward = Pawn ? Pawn->GetActorForwardVector().GetSafeNormal2D() : FVector::ForwardVector;
    const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
    return Pelvis + Forward * CarryPopupOffset.X + Right * CarryPopupOffset.Y + FVector::UpVector * CarryPopupOffset.Z;
}

void ATransferManagerActor::ApplyResultPopupPadding(UUserWidget* Widget)
{
    UBorder* Border = Widget && Widget->WidgetTree ? Cast<UBorder>(Widget->WidgetTree->FindWidget(TEXT("Border_102"))) : nullptr;
    if (!Border) return;
    if (!bStatusPaddingCaptured)
    {
        const FMargin Base = Border->GetPadding();
        StatusBasePadding = FVector4(Base.Left, Base.Top, Base.Right, Base.Bottom);
        bStatusPaddingCaptured = true;
    }
    Border->SetPadding(FMargin(StatusBasePadding.X, StatusBasePadding.Y, StatusBasePadding.Z, StatusBasePadding.W
        + (TrainingStage == EGuidedTrainingStage::Complete ? FMath::Max(0.f, ResultPopupExtraBottomPadding) : 0.f)));
    if (TrainingStage == EGuidedTrainingStage::Complete)
    {
        Border->SetVerticalAlignment(VAlign_Center);
        Widget->WidgetTree->ForEachWidget([](UWidget* Child)
        {
            if (UTextBlock* Text = Cast<UTextBlock>(Child))
            {
                FSlateFontInfo Font = Text->GetFont();
                Font.Size = 44; Font.TypefaceFontName = TEXT("Bold"); Font.OutlineSettings.OutlineSize = 0;
                Text->SetFont(Font); Text->SetJustification(ETextJustify::Center);
                Text->SetAutoWrapText(true);
            }
        });
    }
    if (TrainingStatus)
        TrainingStatus->SetDrawSize(FVector2D(1200.f, TrainingStage == EGuidedTrainingStage::Complete
            ? 600.f + FMath::Max(0.f, ResultPopupExtraBottomPadding - 24.f) : 360.f));
}

void ATransferManagerActor::UpdateStatus()
{
    const bool bVisible = TrainingStage == EGuidedTrainingStage::Transfer
        || TrainingStage == EGuidedTrainingStage::Complete || TrainingStage == EGuidedTrainingStage::Error;
    if (!bStatusAttempted && bVisible)
    {
        bStatusAttempted = true;
        UClass* Class = LoadClass<UUserWidget>(nullptr, TEXT("/Game/Project/Blueprints/UI/WBP_MediaText.WBP_MediaText_C"));
        if (Class)
        {
            TrainingStatus = NewObject<UWidgetComponent>(this, TEXT("TrainingStatus"));
            AddInstanceComponent(TrainingStatus);
            TrainingStatus->SetWidgetClass(Class);
            TrainingStatus->SetDrawSize(FVector2D(1200, 360));
            TrainingStatus->SetWorldScale3D(FVector(StatusPanelScale));
            TrainingStatus->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            TrainingStatus->SetGenerateOverlapEvents(false);
            TrainingStatus->SetCanEverAffectNavigation(false);
            TrainingStatus->SetTwoSided(true);
            TrainingStatus->RegisterComponent();
            TrainingStatus->InitWidget();
        }
    }
    if (!TrainingStatus) return;
    const APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
    const bool bMenu = IsValid(UCNAReliabilityLibrary::ObjectProperty(Pawn, TEXT("MenuReference")));
    TrainingStatus->SetVisibility(bVisible && !bMenu);
    if (!bVisible) return;
    if (PatientRef && PatientRef->IsKinematicCarryActive()) bRetryMessage = false;
    FText Text = TrainingStage == EGuidedTrainingStage::Error ? TrainingError : GetCurrentInstructions();
    if (TrainingStage == EGuidedTrainingStage::Transfer && bRetryMessage) Text = FText::FromString(TEXT("Try again - grab the belt handles and lift slowly."));
    else if (TrainingStage == EGuidedTrainingStage::Transfer && PatientRef && PatientRef->GetInteractionPhase() == EPatientInteractionPhase::BedSeating)
        Text = FText::FromString(TEXT("Please wait while the patient is safely seated."));
    else if (TrainingStage == EGuidedTrainingStage::Transfer && PatientRef && PatientRef->GetInteractionPhase() == EPatientInteractionPhase::BedPreparation)
        Text = FText::FromString(TEXT("Support the patient's head/neck and gently help them sit."));
    UObject* Widget = TrainingStatus->GetUserWidgetObject();
    UFunction* Function = Widget ? Widget->FindFunction(TEXT("SetDisplayText")) : nullptr;
    if (Function)
    {
        FStructOnScope Params(Function);
        if (FTextProperty* Field = FindFProperty<FTextProperty>(Function, TEXT("InText")))
            Field->SetPropertyValue_InContainer(Params.GetStructMemory(), Text);
        Widget->ProcessEvent(Function, Params.GetStructMemory());
    }
    ApplyResultPopupPadding(Cast<UUserWidget>(Widget));
    const bool bCarrying = TrainingStage == EGuidedTrainingStage::Transfer && PatientRef && PatientRef->IsKinematicCarryActive();
    const FVector Location = bCarrying ? CarryStatusLocation(Pawn)
        : (PatientRef ? PatientRef->GetActorLocation() : GetActorLocation()) + StatusPanelOffset;
    TrainingStatus->SetWorldLocation(Location);
    TrainingStatus->SetWorldScale3D(FVector(FMath::Max(.01f, StatusPanelScale)));
    if (Pawn) TrainingStatus->SetWorldRotation((UCNAReliabilityLibrary::TrainingViewLocation(Pawn) - Location).Rotation());
}

void ATransferManagerActor::ScheduleLiftRetry()
{
    if (!bGuidedTraining || TrainingStage != EGuidedTrainingStage::Transfer) return;
    const float Grace = PatientRef && PatientRef->GetPatientCarryComponent()
        ? PatientRef->GetPatientCarryComponent()->ReleaseGracePeriod : .35f;
    GetWorldTimerManager().SetTimer(TrainingTransitionTimer, [this]()
    {
        // A subsequent grab owns its own release grace; this older failure must not reset it.
        if (PatientRef && !PatientRef->IsKinematicCarryActive()) HandleInterruptedLift();
    }, FMath::Max(.001f, Grace), false);
}
