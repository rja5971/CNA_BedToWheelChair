#include "CNAReliabilityLibrary.h"
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
#include "Blueprint/UserWidget.h"

DEFINE_LOG_CATEGORY_STATIC(LogCNARecovery, Log, All);

bool UCNAReliabilityLibrary::IsPatientConversationEnabled(AActor* ChatActor)
{
    const FBoolProperty* Flag = IsValid(ChatActor)
        ? FindFProperty<FBoolProperty>(ChatActor->GetClass(), TEXT("bEnablePatientConversation")) : nullptr;
    return Flag && Flag->GetPropertyValue_InContainer(ChatActor);
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
