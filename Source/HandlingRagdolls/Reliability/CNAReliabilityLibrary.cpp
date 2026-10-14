#include "CNAReliabilityLibrary.h"
#include "../Transfer/TransferManagerActor.h"
#include "TrainingMediaControllerComponent.h"
#include "VRWidgetInputComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "UObject/UnrealType.h"
#include "Components/WidgetComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TimelineComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Blueprint/UserWidget.h"

DEFINE_LOG_CATEGORY_STATIC(LogCNARecovery, Log, All);

bool UCNAReliabilityLibrary::SetPreviewQuizQuestion(UObject* QuizWidget, int32 QuestionIndex)
{
    if (!IsValid(QuizWidget) || QuizWidget->GetClass()->GetName() != TEXT("WBP_QuizPanel_C") || QuestionIndex < 0 || QuestionIndex > 1) return false;
    FIntProperty* Index = FindFProperty<FIntProperty>(QuizWidget->GetClass(), TEXT("CurrentQuestionIndex"));
    UFunction* Load = QuizWidget->FindFunction(TEXT("LoadNextQuestion"));
    if (!Index || !Load || Load->ParmsSize != 0) return false;
    Index->SetPropertyValue_InContainer(QuizWidget, QuestionIndex);
    QuizWidget->ProcessEvent(Load, nullptr);
    return true;
}

UWidgetComponent* UCNAReliabilityLibrary::CreateBeltUIPreview(const UObject* WorldContextObject)
{
    UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
    if (!World || !World->IsGameWorld()) return nullptr;
    UClass* WidgetClass = LoadClass<UUserWidget>(nullptr, TEXT("/Game/Project/Blueprints/UI/WBP_BeltUI.WBP_BeltUI_C"));
    if (!WidgetClass) return nullptr;
    FActorSpawnParameters Parameters; Parameters.ObjectFlags |= RF_Transient;
    AActor* Actor = World->SpawnActor<AActor>(Parameters);
    if (!Actor) return nullptr;
    UWidgetComponent* Panel = NewObject<UWidgetComponent>(Actor, TEXT("BeltUIPreview"));
    Actor->SetRootComponent(Panel); Actor->AddInstanceComponent(Panel);
    Panel->SetWidgetClass(WidgetClass); Panel->SetDrawSize(FVector2D(1200, 500));
    Panel->RegisterComponent(); Panel->InitWidget();
    return Panel;
}

bool UCNAReliabilityLibrary::IsPatientConversationEnabled(AActor* ChatActor)
{
    const FBoolProperty* Flag = IsValid(ChatActor)
        ? FindFProperty<FBoolProperty>(ChatActor->GetClass(), TEXT("bEnablePatientConversation")) : nullptr;
    ATransferManagerActor* Manager = GuidedManager(ChatActor);
    return Flag && Flag->GetPropertyValue_InContainer(ChatActor)
        && (!Manager || Manager->GetTrainingStage() == EGuidedTrainingStage::Conversation);
}

void UCNAReliabilityLibrary::ApplyPatientConversationPolicy(AActor* ChatActor)
{
    UWidgetComponent* Panel = Cast<UWidgetComponent>(ObjectProperty(ChatActor, TEXT("ChatPanel")));
    if (!Panel) return;
    const bool bEnabled = IsPatientConversationEnabled(ChatActor);
    Panel->SetHiddenInGame(!bEnabled);
    Panel->SetCollisionEnabled(bEnabled ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
    if (!bEnabled) Panel->SetVisibility(false);
    if (UUserWidget* Widget = Panel->GetUserWidgetObject())
    {
        Widget->SetIsEnabled(bEnabled);
        Widget->SetVisibility(bEnabled ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    }
}

UObject* UCNAReliabilityLibrary::ObjectProperty(const UObject* Object, FName Name)
{
    if (!IsValid(Object)) return nullptr;
    if (const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Object->GetClass(), Name))
        return Property->GetObjectPropertyValue_InContainer(Object);
    return nullptr;
}

void UCNAReliabilityLibrary::CallNoArgs(UObject* Object, FName FunctionName)
{
    UFunction* Function = IsValid(Object) ? Object->FindFunction(FunctionName) : nullptr;
    if (Function && Function->ParmsSize == 0) Object->ProcessEvent(Function, nullptr);
    else UE_LOG(LogCNARecovery, Error, TEXT("Missing no-argument Blueprint adapter %s on %s"), *FunctionName.ToString(), *GetNameSafe(Object));
}

static UTrainingMediaControllerComponent* FindMediaController(const UObject* Context)
{
    UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(Context, EGetWorldErrorMode::ReturnNull) : nullptr;
    if (World)
    {
        for (TActorIterator<AActor> It(World); It; ++It)
            if (UTrainingMediaControllerComponent* Component = It->FindComponentByClass<UTrainingMediaControllerComponent>()) return Component;
        // Existing placed actors can have a serialized component list predating the SCS migration.
        // Attach to the same handler at runtime rather than rewriting the user's level asset.
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            if (It->GetClass()->GetName() != TEXT("BP_MediaEventHandler_C")) continue;
            UTrainingMediaControllerComponent* Component = NewObject<UTrainingMediaControllerComponent>(*It, TEXT("RuntimeMediaController"));
            It->AddInstanceComponent(Component);
            Component->RegisterComponent();
            return Component;
        }
    }
    return nullptr;
}

void UCNAReliabilityLibrary::RequestPlayback(AActor* Display)
{
    if (UTrainingMediaControllerComponent* Controller = FindMediaController(Display)) Controller->RequestPlayback(Display);
    else UE_LOG(LogCNARecovery, Error, TEXT("No media controller found for %s"), *GetNameSafe(Display));
}

void UCNAReliabilityLibrary::CancelPlayback(AActor* Display)
{
    if (UTrainingMediaControllerComponent* Controller = FindMediaController(Display)) Controller->CancelPlayback(Display);
}

void UCNAReliabilityLibrary::RetryPlayback(const UObject* WorldContextObject)
{
    if (UTrainingMediaControllerComponent* Controller = FindMediaController(WorldContextObject)) Controller->RetryPlayback();
}

static void ClearRecoveryState(UWorld* World)
{
    if (!World) return;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (UVRWidgetInputComponent* Input = It->FindComponentByClass<UVRWidgetInputComponent>()) Input->ReleaseAllPointers();
        if (UTrainingMediaControllerComponent* Media = It->FindComponentByClass<UTrainingMediaControllerComponent>()) Media->CancelPlayback(nullptr);
    }
}

void UCNAReliabilityLibrary::RestartTraining(const UObject* WorldContextObject)
{
    UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
    if (!World) return;
    const FString Level = UGameplayStatics::GetCurrentLevelName(World, true);
    UE_LOG(LogCNARecovery, Display, TEXT("Restart button: reload %s"), *Level);
    ClearRecoveryState(World);
    UGameplayStatics::OpenLevel(World, FName(*Level), true);
}

void UCNAReliabilityLibrary::ResetOrientation(const UObject* WorldContextObject)
{
    UE_LOG(LogCNARecovery, Display, TEXT("Reset Orientation button"));
    // Yaw-only recentering preserves the floor origin and the learner's physical height.
    UHeadMountedDisplayFunctionLibrary::ResetOrientationAndPosition(0.f, EOrientPositionSelector::Orientation);
}

void UCNAReliabilityLibrary::ExitTraining(const UObject* WorldContextObject)
{
    UE_LOG(LogCNARecovery, Display, TEXT("Real life button: exit application"));
    UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
    ClearRecoveryState(World);
    UKismetSystemLibrary::QuitGame(WorldContextObject, UGameplayStatics::GetPlayerController(WorldContextObject, 0), EQuitPreference::Quit, false);
}

void UCNAReliabilityLibrary::StopMenuInteraction(AActor* Menu)
{
    if (!IsValid(Menu)) return;
    for (TActorIterator<APawn> It(Menu->GetWorld()); It; ++It)
        if (UVRWidgetInputComponent* Input = It->FindComponentByClass<UVRWidgetInputComponent>()) Input->StopMenuInteraction(Menu);
}

ATransferManagerActor* UCNAReliabilityLibrary::GuidedManager(const UObject* Context)
{
    UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(Context, EGetWorldErrorMode::ReturnNull) : nullptr;
    if (World) for (TActorIterator<ATransferManagerActor> It(World); It; ++It) if (It->bGuidedTraining) return *It;
    return nullptr;
}
void UCNAReliabilityLibrary::SubmitGuidedQuiz(UObject* Widget, int32 Answer)
{
    if (ATransferManagerActor* Manager = GuidedManager(Widget)) Manager->SubmitQuiz(Widget, Answer);
}
void UCNAReliabilityLibrary::DismissGuidedWrongAnswer(const UObject* Context)
{
    if (ATransferManagerActor* Manager = GuidedManager(Context)) Manager->DismissWrongAnswer();
}
bool UCNAReliabilityLibrary::BeginGuidedRoomTransition(const UObject* Context)
{
    ATransferManagerActor* Manager = GuidedManager(Context);
    if (Manager && !IsValid(ObjectProperty(Context, TEXT("TeleportTarget"))))
    {
        UE_LOG(LogCNARecovery, Warning, TEXT("GuidedTraining: Continue rejected because room teleport target is missing"));
        return false;
    }
    return !Manager || Manager->BeginRoomTransition();
}
void UCNAReliabilityLibrary::FinishGuidedRoomTransition(const UObject* Context)
{
    if (ATransferManagerActor* Manager = GuidedManager(Context)) Manager->FinishRoomTransition();
}
bool UCNAReliabilityLibrary::BeginGuidedConversationResponse(const UObject* Context)
{
    AActor* Chat = Cast<AActor>(ObjectProperty(Context, TEXT("OwningActor")));
    if (!IsPatientConversationEnabled(Chat))
    {
        ApplyPatientConversationPolicy(Chat);
        return false;
    }
    ATransferManagerActor* Manager = GuidedManager(Context);
    return !Manager || Manager->BeginConversationResponse();
}
bool UCNAReliabilityLibrary::BeginGuidedTransferLesson(const UObject* Context)
{
    AActor* Chat = Cast<AActor>(ObjectProperty(Context, TEXT("OwningActor")));
    if (!IsPatientConversationEnabled(Chat))
    {
        ApplyPatientConversationPolicy(Chat);
        return false;
    }
    ATransferManagerActor* Manager = GuidedManager(Context);
    return !Manager || Manager->BeginTransferLesson();
}
bool UCNAReliabilityLibrary::IsTrainingMovementAllowed(const UObject* Context)
{
    ATransferManagerActor* Manager = GuidedManager(Context);
    return !Manager || Manager->IsTrainingMovementAllowed();
}
FVector UCNAReliabilityLibrary::TrainingViewLocation(const APawn* Pawn)
{
    if (const USceneComponent* Camera = Cast<USceneComponent>(ObjectProperty(Pawn, TEXT("Camera"))))
        return Camera->GetComponentLocation();
    return Pawn ? Pawn->GetPawnViewLocation() : FVector::ZeroVector;
}
bool UCNAReliabilityLibrary::ValidateTrainingTeleportLocation(const UObject* Context, bool bNavigationValid, FVector ProjectedLocation)
{
    if (!bNavigationValid) return false;
    ATransferManagerActor* Manager = GuidedManager(Context);
    if (!Manager) return true;
    const APawn* Pawn = UGameplayStatics::GetPlayerPawn(Context, 0);
    if (!Pawn) return false;
    // Match TryTeleport's room-scale camera correction, then bound the resulting viewpoint.
    if (const USceneComponent* Camera = Cast<USceneComponent>(ObjectProperty(Pawn, TEXT("Camera"))))
    {
        const FVector Relative = Camera->GetRelativeLocation();
        ProjectedLocation -= FRotator(0.f, Pawn->GetActorRotation().Yaw, 0.f).RotateVector(FVector(Relative.X, Relative.Y, 0.f));
    }
    ProjectedLocation += TrainingViewLocation(Pawn) - Pawn->GetActorLocation();
    return Manager->IsTrainingLocationAllowed(ProjectedLocation);
}
bool UCNAReliabilityLibrary::IsPendingTrainingTeleportAllowed(const UObject* Context)
{
    if (!IsTrainingMovementAllowed(Context)) return false;
    if (!GuidedManager(Context)) return true;
    const APawn* Pawn = UGameplayStatics::GetPlayerPawn(Context, 0);
    const FStructProperty* Target = Pawn ? FindFProperty<FStructProperty>(Pawn->GetClass(), TEXT("ProjectedTeleportLocation")) : nullptr;
    return Target && ValidateTrainingTeleportLocation(Context, true, *Target->ContainerPtrToValuePtr<FVector>(Pawn));
}
void UCNAReliabilityLibrary::AddTrainingMovementInput(APawn* Pawn, FVector WorldDirection, float ScaleValue, bool bForce)
{
    if (!Pawn || !IsTrainingMovementAllowed(Pawn)) return;
    ATransferManagerActor* Manager = GuidedManager(Pawn);
    if (!Manager) { Pawn->AddMovementInput(WorldDirection, ScaleValue, bForce); return; }
    WorldDirection.Z = 0.f;
    const FVector Delta = WorldDirection.GetSafeNormal() * FMath::Clamp(ScaleValue, -1.f, 1.f)
        * Manager->TrainingSmoothMoveSpeed * Pawn->GetWorld()->GetDeltaSeconds();
    Pawn->AddActorWorldOffset(Manager->ClampTrainingMovementDelta(Pawn, Delta), true);
}
void UCNAReliabilityLibrary::EnsureTrainingFade(AActor* Pawn)
{
    if (!Pawn || ObjectProperty(Pawn, TEXT("FadeMaterialInstance"))) return;
    UStaticMeshComponent* Sphere = Cast<UStaticMeshComponent>(ObjectProperty(Pawn, TEXT("FadeSphere")));
    FObjectPropertyBase* Field = FindFProperty<FObjectPropertyBase>(Pawn->GetClass(), TEXT("FadeMaterialInstance"));
    if (Sphere && Field)
    {
        UMaterialInstanceDynamic* Material = Sphere->CreateDynamicMaterialInstance(0);
        if (Material)
        {
            Material->SetScalarParameterValue(TEXT("Opacity"), 0.f);
            Field->SetObjectPropertyValue_InContainer(Pawn, Material);
        }
    }
}
void UCNAReliabilityLibrary::TrainingFadeOut(const UObject* Context)
{
    APawn* Pawn = UGameplayStatics::GetPlayerPawn(Context, 0);
    EnsureTrainingFade(Pawn);
    if (UTimelineComponent* Timeline = Cast<UTimelineComponent>(ObjectProperty(Pawn, TEXT("FadeTimeline")))) Timeline->SetPlayRate(Timeline->GetTimelineLength());
    CallNoArgs(Pawn, TEXT("FadeOut"));
}
void UCNAReliabilityLibrary::TrainingFadeIn(const UObject* Context)
{
    APawn* Pawn = UGameplayStatics::GetPlayerPawn(Context, 0);
    EnsureTrainingFade(Pawn);
    if (UTimelineComponent* Timeline = Cast<UTimelineComponent>(ObjectProperty(Pawn, TEXT("FadeTimeline")))) Timeline->SetPlayRate(Timeline->GetTimelineLength() / .5f);
    CallNoArgs(Pawn, TEXT("FadeIn"));
}

void UCNAReliabilityLibrary::RequestGuidedConversationPanel(AActor* ChatActor)
{
    ApplyPatientConversationPolicy(ChatActor);
    if (UWidgetComponent* Panel = Cast<UWidgetComponent>(ObjectProperty(ChatActor, TEXT("ChatPanel"))))
        Panel->SetVisibility(IsPatientConversationEnabled(ChatActor));
}
