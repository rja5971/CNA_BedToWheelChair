#include "TrainingPlaybackSession.h"
#include "Misc/AutomationTest.h"
#include "EnhancedInputDeveloperSettings.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "VRWidgetInputComponent.h"
#include "MenuCursorInteraction.h"
#include "Components/WidgetComponent.h"
#include "CNAReliabilityLibrary.h"
#include "Components/WidgetInteractionComponent.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCNAPlaybackRecoveryTest, "CNA.Reliability.Media.RecoveryPolicy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCNAPlaybackRecoveryTest::RunTest(const FString& Parameters)
{
    FTrainingPlaybackSession Session;
    Session.Start(0, true);
    TestFalse(TEXT("Startup allowed before ten seconds"), Session.TimedOut(9.5));
    TestTrue(TEXT("Startup fails at ten seconds"), Session.TimedOut(10));
    TestTrue(TEXT("First failure accepted"), Session.Fail());
    TestTrue(TEXT("Exactly one automatic retry scheduled"), Session.State == FTrainingPlaybackSession::EState::RetryPending);
    TestFalse(TEXT("Duplicate failure while waiting cannot consume another retry"), Session.Fail());
    Session.Start(10.5, false);
    TestTrue(TEXT("Media open accepted"), Session.Opened(11));
    TestFalse(TEXT("Duplicate media-open event ignored"), Session.Opened(11));
    TestFalse(TEXT("Title frame without progress cannot complete training"), Session.Complete());
    Session.Progress(1, 12);
    TestFalse(TEXT("Progress resets stall deadline"), Session.TimedOut(16.5));
    TestTrue(TEXT("Five seconds without progress is a stall"), Session.TimedOut(17));
    Session.Fail();
    TestTrue(TEXT("Second failure requires manual retry"), Session.State == FTrainingPlaybackSession::EState::Failed);
    TestFalse(TEXT("Failure must not complete training"), Session.Complete());
    Session.Start(20, true);
    TestEqual(TEXT("Manual retry gets a fresh automatic retry budget"), Session.AutomaticRetries, 0);
    Session.Opened(20.5);
    Session.Progress(50, 21);
    TestTrue(TEXT("Successful video can complete"), Session.Complete());
    TestFalse(TEXT("Completion dispatched exactly once"), Session.Complete());
    Session.Start(30, true);
    Session.Cancel();
    TestFalse(TEXT("Canceled request cannot accept late open"), Session.Opened(31));
    TestFalse(TEXT("Canceled request cannot accept late completion"), Session.Complete());
    TestFalse(TEXT("Canceled request has no timeout"), Session.TimedOut(100));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCNAOpenXRClickRegistrationTest, "CNA.Reliability.Input.OpenXRStartupRegistration", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCNAOpenXRClickRegistrationTest::RunTest(const FString& Parameters)
{
    const UEnhancedInputDeveloperSettings* Settings = GetDefault<UEnhancedInputDeveloperSettings>();
    TestTrue(TEXT("OpenXR must read startup mapping contexts"), !!Settings->bEnableDefaultMappingContexts);
    const FSoftObjectPath Path(TEXT("/Game/VRTemplate/Input/IMC_UIInteract.IMC_UIInteract"));
    const FDefaultContextSetting* Entry = Settings->DefaultMappingContexts.FindByPredicate(
        [&Path](const FDefaultContextSetting& Value) { return Value.InputMappingContext.ToSoftObjectPath() == Path; });
    if (!TestNotNull(TEXT("Widget triggers registered before OpenXR action sets attach"), Entry)) return false;
    TestEqual(TEXT("Startup action-set priority matches pawn priority"), Entry->Priority, 2);
    const UInputMappingContext* Context = Entry->InputMappingContext.LoadSynchronous();
    if (!TestNotNull(TEXT("Registered widget context can be loaded"), Context)) return false;
    for (const TCHAR* Hand : { TEXT("Left"), TEXT("Right") })
    {
        const FString Key = FString(TEXT("OculusTouch_")) + Hand + TEXT("_Trigger_Axis");
        const FString Action = FString(TEXT("/Game/VRTemplate/Input/Actions/IA_UIInteract_")) + Hand + TEXT(".IA_UIInteract_") + Hand;
        const bool bFound = Context->GetMappings().ContainsByPredicate([&Key, &Action](const FEnhancedActionKeyMapping& Mapping)
        { return Mapping.Key.GetFName() == FName(*Key) && Mapping.Action && Mapping.Action->GetPathName() == Action; });
        TestTrue(FString(TEXT("Quest trigger binding present for ")) + Hand, bFound);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCNAMenuCursorRoutingTest, "CNA.Reliability.Input.MenuCursorRouting", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCNAMenuCursorRoutingTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    UClass* PawnClass = LoadObject<UClass>(nullptr, TEXT("/Game/VRTemplate/Blueprints/VRPawn.VRPawn_C"));
    UClass* MenuClass = LoadObject<UClass>(nullptr, TEXT("/Game/VRTemplate/Blueprints/Menu.Menu_C"));
    AActor* Pawn = PawnClass ? World->SpawnActor<AActor>(PawnClass) : nullptr;
    AActor* Menu = MenuClass ? World->SpawnActor<AActor>(MenuClass) : nullptr;
    if (!TestNotNull(TEXT("Actual VR pawn"), Pawn) || !TestNotNull(TEXT("Actual cursor menu"), Menu)) { World->DestroyWorld(false); return false; }
    UWidgetInteractionComponent* Left = Cast<UWidgetInteractionComponent>(UCNAReliabilityLibrary::ObjectProperty(Pawn, TEXT("WidgetInteractionLeft")));
    UWidgetInteractionComponent* Right = Cast<UWidgetInteractionComponent>(UCNAReliabilityLibrary::ObjectProperty(Pawn, TEXT("WidgetInteractionRight")));
    TestNotNull(TEXT("Left pawn pointer"), Left); TestNotNull(TEXT("Right pawn pointer"), Right);
    TestTrue(TEXT("Independent controller pointers have distinct indices"), Left && Right && Left->PointerIndex != Right->PointerIndex);
    TestEqual(TEXT("Training left trigger uses its laser"), UVRWidgetInputComponent::ResolveInteractionForPawn(Pawn, false), Left);
    TestEqual(TEXT("Training right trigger uses its laser"), UVRWidgetInputComponent::ResolveInteractionForPawn(Pawn, true), Right);
    FObjectPropertyBase* MenuRef = FindFProperty<FObjectPropertyBase>(PawnClass, TEXT("MenuReference"));
    if (TestNotNull(TEXT("Menu reference"), MenuRef))
    {
        MenuRef->SetObjectPropertyValue_InContainer(Pawn, Menu);
        UVRWidgetInputComponent* Input = Pawn->FindComponentByClass<UVRWidgetInputComponent>();
        Input->UpdateMenuCursor(Menu, FVector2D::ZeroVector, 0.f);
        UMenuCursorInteraction* Pointer = Cast<UMenuCursorInteraction>(UVRWidgetInputComponent::ResolveInteractionForPawn(Pawn, false));
        TestNotNull(TEXT("Dedicated cursor pointer initialized without stick movement"), Pointer);
        TestNull(TEXT("Right trigger cannot click left-opened menu"), UVRWidgetInputComponent::ResolveInteractionForPawn(Pawn, true));
        TestTrue(TEXT("Dot pointer has a distinct virtual user and pointer index"), Pointer && Left && Right && Pointer->VirtualUserIndex != Left->VirtualUserIndex && Pointer->PointerIndex != Left->PointerIndex && Pointer->PointerIndex != Right->PointerIndex);
        TestTrue(TEXT("Dot uses a custom menu-plane hit, independent of controller rays"), Pointer && Pointer->InteractionSource == EWidgetInteractionSource::Custom);
        UWidgetComponent* Widget = Cast<UWidgetComponent>(UCNAReliabilityLibrary::ObjectProperty(Menu, TEXT("Widget")));
        USceneComponent* Dot = Cast<USceneComponent>(UCNAReliabilityLibrary::ObjectProperty(Menu, TEXT("Cursor")));
        TestTrue(TEXT("Dot visible immediately on open"), Dot && Dot->IsVisible());
        const FVector Before = Dot->GetComponentLocation();
        Input->UpdateMenuCursorFromHands(Menu, FVector2D::ZeroVector, FVector2D(1, 1), 0.1f);
        TestTrue(TEXT("Right stick cannot move left-opened menu"), Dot->GetComponentLocation().Equals(Before));
        Input->UpdateMenuCursorFromHands(Menu, FVector2D(1, 1), FVector2D::ZeroVector, 0.1f);
        TestTrue(TEXT("Left stick moves its menu on both axes"), !Dot->GetComponentLocation().Equals(Before));
        FBoolProperty* OwnerHand = FindFProperty<FBoolProperty>(MenuClass, TEXT("bActiveMenuHandRight"));
        if (TestNotNull(TEXT("Menu stores its opening hand"), OwnerHand))
        {
            OwnerHand->SetPropertyValue_InContainer(Menu, true);
            TestEqual(TEXT("Right trigger controls right-opened menu"), UVRWidgetInputComponent::ResolveInteractionForPawn(Pawn, true), static_cast<UWidgetInteractionComponent*>(Pointer));
            TestNull(TEXT("Left trigger cannot click right-opened menu"), UVRWidgetInputComponent::ResolveInteractionForPawn(Pawn, false));
            const FVector RightBefore = Dot->GetComponentLocation();
            Input->UpdateMenuCursorFromHands(Menu, FVector2D(1, 1), FVector2D::ZeroVector, 0.1f);
            TestTrue(TEXT("Left stick cannot move right-opened menu"), Dot->GetComponentLocation().Equals(RightBefore));
            Input->UpdateMenuCursorFromHands(Menu, FVector2D::ZeroVector, FVector2D(-1, -1), 0.1f);
            TestTrue(TEXT("Right stick moves its menu on both axes"), !Dot->GetComponentLocation().Equals(RightBefore));
            OwnerHand->SetPropertyValue_InContainer(Menu, false);
        }
        Input->SetMenuCursorPosition(FVector2D(0.5, 0.5));
        TestTrue(TEXT("Centered dot sits in front of menu plane"), Dot->GetComponentLocation().Equals(Widget->GetComponentLocation() + Widget->GetForwardVector(), 0.01));
        Input->StopMenuInteraction(Menu);
        TestNull(TEXT("Closing animation accepts no new clicks"), UVRWidgetInputComponent::ResolveInteractionForPawn(Pawn, false));
        TestFalse(TEXT("Dot hidden on close"), Dot->IsVisible());
        Menu->Destroy();
        TestEqual(TEXT("Closing restores left training laser"), UVRWidgetInputComponent::ResolveInteractionForPawn(Pawn, false), Left);
        TestEqual(TEXT("Closing restores right training laser"), UVRWidgetInputComponent::ResolveInteractionForPawn(Pawn, true), Right);
    }
    World->DestroyWorld(false);
    return true;
}
#endif
