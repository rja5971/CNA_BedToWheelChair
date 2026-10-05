#include "VRWidgetInputComponent.h"
#include "MenuCursorInteraction.h"
#include "EnhancedPlayerInput.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "CNAReliabilityLibrary.h"
#include "Components/WidgetInteractionComponent.h"
#include "Components/WidgetComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputDeveloperSettings.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/LocalPlayer.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "InputCoreTypes.h"
#include "Misc/CoreDelegates.h"
#include "UObject/UnrealType.h"
#include "UObject/StructOnScope.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogCNAWidgetInput, Log, All);

static bool IsRightHandMenu(const AActor* Menu)
{
    const FBoolProperty* Hand = IsValid(Menu) ? FindFProperty<FBoolProperty>(Menu->GetClass(), TEXT("bActiveMenuHandRight")) : nullptr;
    return Hand && Hand->GetPropertyValue_InContainer(Menu);
}

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommandWithWorld InspectMenu(TEXT("cna.UI.InspectMenu"), TEXT("Open the actual cursor menu and inspect its click routing."),
    FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
    {
        APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0);
        UFunction* Toggle = Pawn ? Pawn->FindFunction(TEXT("ToggleMenu")) : nullptr;
        if (!Toggle) return;
        FStructOnScope Params(Toggle);
        Pawn->ProcessEvent(Toggle, Params.GetStructMemory());
        FTimerHandle Check;
        World->GetTimerManager().SetTimer(Check, [Pawn]()
        {
            AActor* Menu = Cast<AActor>(UCNAReliabilityLibrary::ObjectProperty(Pawn, TEXT("MenuReference")));
            UE_LOG(LogCNAWidgetInput, Display, TEXT("MENU INSPECT menu=%s class=%s"), *GetNameSafe(Menu), *GetNameSafe(Menu ? Menu->GetClass() : nullptr));
            if (Menu) for (TFieldIterator<FProperty> It(Menu->GetClass()); It; ++It)
            {
                if (!It->GetName().Contains(TEXT("Interaction"))) continue;
                FString Value; It->ExportText_InContainer(0, Value, Menu, nullptr, Menu, PPF_None);
                UE_LOG(LogCNAWidgetInput, Display, TEXT("MENU PROPERTY %s type=%s value=%s"), *It->GetName(), *It->GetCPPType(), *Value);
            }
            TArray<UWidgetInteractionComponent*> Pointers; Pawn->GetComponents(Pointers);
            for (UWidgetInteractionComponent* Pointer : Pointers)
                UE_LOG(LogCNAWidgetInput, Display, TEXT("MENU POINTER %s index=%d parent=%s trace=%d active=%d"), *Pointer->GetName(), Pointer->PointerIndex, *GetNameSafe(Pointer->GetAttachParent()), int(Pointer->TraceChannel), Pointer->IsActive());
            for (UWidgetInteractionComponent* Pointer : Pointers) UE_LOG(LogCNAWidgetInput, Display, TEXT("MENU HIT %s target=%s"), *Pointer->GetName(), *GetNameSafe(Pointer->GetLastHitResult().GetComponent()));
            if (Menu) if (UPrimitiveComponent* Cursor = Cast<UPrimitiveComponent>(UCNAReliabilityLibrary::ObjectProperty(Menu, TEXT("Cursor")))) UE_LOG(LogCNAWidgetInput, Display, TEXT("MENU CURSOR collision=%d worldDynamicResponse=%d visibilityResponse=%d"), int(Cursor->GetCollisionEnabled()), int(Cursor->GetCollisionResponseToChannel(ECC_WorldDynamic)), int(Cursor->GetCollisionResponseToChannel(ECC_Visibility)));
            FPlatformMisc::RequestExit(false);
        }, 1.f, false);
    }));
static FAutoConsoleCommandWithWorld SmokeCursorClicks(TEXT("cna.UI.SmokeCursorClicks"),
    TEXT("Open the real menu, click all three real Slate buttons through the pawn cursor, verify close/reopen, then exit."),
    FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
    {
        APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0);
        UFunction* Toggle = Pawn ? Pawn->FindFunction(TEXT("ToggleMenu")) : nullptr;
        if (!Toggle) { FPlatformMisc::RequestExitWithStatus(false, 1); return; }
        FStructOnScope Params(Toggle);
        Pawn->ProcessEvent(Toggle, Params.GetStructMemory());
        FTimerHandle Check;
        World->GetTimerManager().SetTimer(Check, [Pawn, World]()
        {
            AActor* Menu = Cast<AActor>(UCNAReliabilityLibrary::ObjectProperty(Pawn, TEXT("MenuReference")));
            UVRWidgetInputComponent* Input = Pawn->FindComponentByClass<UVRWidgetInputComponent>();
            UWidgetComponent* Widget = Cast<UWidgetComponent>(UCNAReliabilityLibrary::ObjectProperty(Menu, TEXT("Widget")));
            UUserWidget* UI = Widget ? Widget->GetUserWidgetObject() : nullptr;
            UMenuCursorInteraction* Pointer = Cast<UMenuCursorInteraction>(UVRWidgetInputComponent::ResolveInteractionForPawn(Pawn, false));
            bool bPassed = Input && UI && Pointer && Widget->GetCurrentDrawSize().Y > 0;
            const TCHAR* Names[] = { TEXT("ResetOrientationButton"), TEXT("RestartButton"), TEXT("ExitButton") };
            UButton* Buttons[3] = {};
            for (int32 Index = 0; bPassed && Index < 3; ++Index)
            {
                Buttons[Index] = Cast<UButton>(UI->GetWidgetFromName(FName(Names[Index])));
                if (!Buttons[Index]) { bPassed = false; break; }
                // All actions are replaced before any click, only in this test
                // instance, so a bad hit cannot accidentally reload or quit early.
                Buttons[Index]->OnClicked.Clear();
                if (Index == 0) Buttons[Index]->OnClicked.AddDynamic(Pointer, &UMenuCursorInteraction::ObserveReset);
                if (Index == 1) Buttons[Index]->OnClicked.AddDynamic(Pointer, &UMenuCursorInteraction::ObserveRestart);
                if (Index == 2) Buttons[Index]->OnClicked.AddDynamic(Pointer, &UMenuCursorInteraction::ObserveExit);
            }
            FBoolProperty* OwnerHand = Menu ? FindFProperty<FBoolProperty>(Menu->GetClass(), TEXT("bActiveMenuHandRight")) : nullptr;
            bPassed &= OwnerHand != nullptr;
            for (int32 Hand = 0; bPassed && Hand < 2; ++Hand)
            {
                OwnerHand->SetPropertyValue_InContainer(Menu, Hand == 1);
                Pointer->ResetClicks = Pointer->RestartClicks = Pointer->ExitClicks = 0;
                for (int32 Index = 0; bPassed && Index < 3; ++Index)
                {
                    UButton* Button = Buttons[Index];
                    const FGeometry& Geometry = Button->GetCachedGeometry();
                    const FVector2D Center = UI->GetCachedGeometry().AbsoluteToLocal(Geometry.LocalToAbsolute(Geometry.GetLocalSize() * 0.5));
                    Input->SetMenuCursorPosition(Center / Widget->GetCurrentDrawSize());
                    if (Hand == 0) Input->ClickRightForTest(); else Input->ClickLeftForTest();
                    bPassed &= Pointer->ResetClicks == (Index > 0 ? 1 : 0) && Pointer->RestartClicks == (Index > 1 ? 1 : 0) && Pointer->ExitClicks == 0;
                    if (Hand == 0) Input->ClickLeftForTest(); else Input->ClickRightForTest();
                    bPassed &= Pointer->ResetClicks == 1 && Pointer->RestartClicks == (Index >= 1 ? 1 : 0) && Pointer->ExitClicks == (Index >= 2 ? 1 : 0);
                    UE_LOG(LogCNAWidgetInput, Display, TEXT("SLATE CURSOR CLICK owner=%s %s counts=(%d,%d,%d) other-hand-blocked %s"),
                        Hand == 0 ? TEXT("left") : TEXT("right"), Names[Index], Pointer->ResetClicks, Pointer->RestartClicks, Pointer->ExitClicks, bPassed ? TEXT("PASS") : TEXT("FAIL"));
                }
            }
            if (OwnerHand) OwnerHand->SetPropertyValue_InContainer(Menu, false);
            if (bPassed)
            {
                UCNAReliabilityLibrary::CallNoArgs(Menu, TEXT("CloseMenu"));
                bPassed &= UVRWidgetInputComponent::ResolveInteractionForPawn(Pawn, false) == nullptr;
            }
            FTimerHandle Reopen;
            World->GetTimerManager().SetTimer(Reopen, [Pawn, World, bPassed]()
            {
                bool bResult = bPassed && UVRWidgetInputComponent::ResolveInteractionForPawn(Pawn, false) ==
                    Cast<UWidgetInteractionComponent>(UCNAReliabilityLibrary::ObjectProperty(Pawn, TEXT("WidgetInteractionLeft")));
                UFunction* ToggleAgain = Pawn->FindFunction(TEXT("ToggleMenu"));
                FStructOnScope Args(ToggleAgain); Pawn->ProcessEvent(ToggleAgain, Args.GetStructMemory());
                bResult &= Cast<UMenuCursorInteraction>(UVRWidgetInputComponent::ResolveInteractionForPawn(Pawn, false)) != nullptr;
                FTimerHandle Finish;
                World->GetTimerManager().SetTimer(Finish, [Pawn, bResult]()
                {
                    UMenuCursorInteraction* NewPointer = Cast<UMenuCursorInteraction>(UVRWidgetInputComponent::ResolveInteractionForPawn(Pawn, false));
                    AActor* Menu = Cast<AActor>(UCNAReliabilityLibrary::ObjectProperty(Pawn, TEXT("MenuReference")));
                    USceneComponent* Dot = Cast<USceneComponent>(UCNAReliabilityLibrary::ObjectProperty(Menu, TEXT("Cursor")));
                    const bool bFinal = bResult && NewPointer && NewPointer->IsOverInteractableWidget() && Dot && Dot->IsVisible();
                    UE_LOG(LogCNAWidgetInput, Display, TEXT("Runtime real Slate clicks, menu close and cursor reopen: %s"), bFinal ? TEXT("PASS") : TEXT("FAIL"));
                    FPlatformMisc::RequestExitWithStatus(false, bFinal ? 0 : 1);
                }, 1.f, false);
            }, 1.f, false);
        }, 2.f, false);
    }));

#endif

UVRWidgetInputComponent::UVRWidgetInputComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
    WidgetContext = FSoftObjectPath(TEXT("/Game/VRTemplate/Input/IMC_UIInteract.IMC_UIInteract"));
    LeftAction = FSoftObjectPath(TEXT("/Game/VRTemplate/Input/Actions/IA_UIInteract_Left.IA_UIInteract_Left"));
    RightAction = FSoftObjectPath(TEXT("/Game/VRTemplate/Input/Actions/IA_UIInteract_Right.IA_UIInteract_Right"));
}

void UVRWidgetInputComponent::BeginPlay()
{
    Super::BeginPlay();
    DeactivateHandle = FCoreDelegates::ApplicationWillDeactivateDelegate.AddUObject(this, &UVRWidgetInputComponent::Suspend);
    BackgroundHandle = FCoreDelegates::ApplicationWillEnterBackgroundDelegate.AddUObject(this, &UVRWidgetInputComponent::Suspend);
    ReactivateHandle = FCoreDelegates::ApplicationHasReactivatedDelegate.AddUObject(this, &UVRWidgetInputComponent::Resume);
    ForegroundHandle = FCoreDelegates::ApplicationHasEnteredForegroundDelegate.AddUObject(this, &UVRWidgetInputComponent::Resume);
    TryBindInput();
}

bool UVRWidgetInputComponent::TryBindInput()
{
    APawn* Pawn = Cast<APawn>(GetOwner());
    APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
    UEnhancedInputComponent* Input = Pawn ? Cast<UEnhancedInputComponent>(Pawn->InputComponent) : nullptr;
    if (!PC || !PC->IsLocalController() || !Input || !PC->GetLocalPlayer()) return false;
    if (BoundInput == Input) return true;
    UnbindInput();
    UInputMappingContext* Context = WidgetContext.LoadSynchronous();
    UInputAction* Left = LeftAction.LoadSynchronous();
    UInputAction* Right = RightAction.LoadSynchronous();
    CursorLeftAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/VRTemplate/Input/Actions/IA_Menu_Cursor_Left.IA_Menu_Cursor_Left"));
    CursorRightAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/VRTemplate/Input/Actions/IA_Menu_Cursor_Right.IA_Menu_Cursor_Right"));
    if (!Context || !Left || !Right) { UE_LOG(LogCNAWidgetInput, Error, TEXT("Widget interaction input assets missing")); SetComponentTickEnabled(false); return false; }
    // OpenXR creates its action sets from startup defaults, before pawn binding.
    // Adding a context to the local player alone cannot add actions to an attached XR session.
    const UEnhancedInputDeveloperSettings* Settings = GetDefault<UEnhancedInputDeveloperSettings>();
    const bool bRegisteredForXR = Settings->bEnableDefaultMappingContexts && Settings->DefaultMappingContexts.ContainsByPredicate(
        [this](const FDefaultContextSetting& Setting) { return Setting.InputMappingContext.ToSoftObjectPath() == WidgetContext.ToSoftObjectPath(); });
    if (!bRegisteredForXR) UE_LOG(LogCNAWidgetInput, Error, TEXT("Widget context missing from OpenXR startup mapping contexts"));
    UE_LOG(LogCNAWidgetInput, Display, TEXT("Widget context=%s priority=2 OpenXRStartupRegistered=%d"), *GetPathNameSafe(Context), bRegisteredForXR);
    InputSubsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer());
    if (!InputSubsystem.IsValid()) return false;
    // This context only contains trigger clicks. Menu thumbstick mappings remain separate.
    FModifyContextOptions Options; Options.bIgnoreAllPressedKeysUntilRelease = true;
    InputSubsystem->AddMappingContext(Context, 2, Options);
    BoundInput = Input;
    BindingHandles.Add(Input->BindAction(Left, ETriggerEvent::Started, this, &UVRWidgetInputComponent::LeftPressed).GetHandle());
    BindingHandles.Add(Input->BindAction(Left, ETriggerEvent::Completed, this, &UVRWidgetInputComponent::LeftReleased).GetHandle());
    BindingHandles.Add(Input->BindAction(Left, ETriggerEvent::Canceled, this, &UVRWidgetInputComponent::LeftReleased).GetHandle());
    BindingHandles.Add(Input->BindAction(Right, ETriggerEvent::Started, this, &UVRWidgetInputComponent::RightPressed).GetHandle());
    BindingHandles.Add(Input->BindAction(Right, ETriggerEvent::Completed, this, &UVRWidgetInputComponent::RightReleased).GetHandle());
    BindingHandles.Add(Input->BindAction(Right, ETriggerEvent::Canceled, this, &UVRWidgetInputComponent::RightReleased).GetHandle());
    UE_LOG(LogCNAWidgetInput, Display, TEXT("Pawn=%s owns widget click input"), *GetNameSafe(Pawn));
    return true;
}

UWidgetInteractionComponent* UVRWidgetInputComponent::ResolveInteraction(bool bRight) const
{
    return ResolveInteractionForPawn(GetOwner(), bRight);
}

UWidgetInteractionComponent* UVRWidgetInputComponent::ResolveInteractionForPawn(AActor* Pawn, bool bRight)
{
    AActor* Menu = Cast<AActor>(UCNAReliabilityLibrary::ObjectProperty(Pawn, TEXT("MenuReference")));
    if (IsValid(Menu))
    {
        UVRWidgetInputComponent* Input = Pawn->FindComponentByClass<UVRWidgetInputComponent>();
        if (!Input) return nullptr;
        if (Input->CursorMenu.Get() != Menu) Input->UpdateMenuCursor(Menu, FVector2D::ZeroVector, 0.f);
        if (Input->bMenuClosing || bRight != IsRightHandMenu(Menu)) return nullptr;
        return Input->MenuPointer.Get();
    }
    return Cast<UWidgetInteractionComponent>(UCNAReliabilityLibrary::ObjectProperty(Pawn,
        bRight ? TEXT("WidgetInteractionRight") : TEXT("WidgetInteractionLeft")));
}

void UVRWidgetInputComponent::Press(bool bRight)
{
    const int32 Hand = bRight ? 1 : 0;
    bool bUseFocus = false, bHasFocus = true;
    UHeadMountedDisplayFunctionLibrary::GetVRFocusState(bUseFocus, bHasFocus);
    UE_LOG(LogCNAWidgetInput, Display, TEXT("Trigger action started hand=%s suspended=%d focusRequired=%d hasFocus=%d pointerHeld=%d"),
        bRight ? TEXT("right") : TEXT("left"), bSuspended, bUseFocus, bHasFocus, PressedPointers[Hand].IsValid());
    if (bSuspended || (bUseFocus && !bHasFocus) || PressedPointers[Hand].IsValid()) return;
    UWidgetInteractionComponent* Interaction = ResolveInteraction(bRight);
    if (MenuPointer.IsValid() && Interaction == MenuPointer.Get()) MenuPointer->UpdateWidgetHit(MenuWidget.Get(), MenuCursorPosition);
    UE_LOG(LogCNAWidgetInput, Display, TEXT("Trigger press hand=%s interaction=%s widget=%s interactable=%d"),
        bRight ? TEXT("right") : TEXT("left"), *GetNameSafe(Interaction), Interaction ? *GetNameSafe(Interaction->GetHoveredWidgetComponent()) : TEXT("none"), Interaction && Interaction->IsOverInteractableWidget());
    if (!Interaction || !Interaction->IsOverInteractableWidget()) return;
    // A menu is owned by its opening hand; retain this safeguard during changes.
    if (PressedPointers[1 - Hand].Get() == Interaction) return;
    PressedPointers[Hand] = Interaction;
    Interaction->PressPointerKey(EKeys::LeftMouseButton);
}

void UVRWidgetInputComponent::Release(bool bRight)
{
    const int32 Hand = bRight ? 1 : 0;
    // Release the component that received the press even if cursor ownership changed.
    UWidgetInteractionComponent* Interaction = PressedPointers[Hand].Get();
    PressedPointers[Hand].Reset();
    if (Interaction)
    {
        Interaction->ReleasePointerKey(EKeys::LeftMouseButton);
        UE_LOG(LogCNAWidgetInput, Display, TEXT("Pointer release hand=%s interaction=%s"), bRight ? TEXT("right") : TEXT("left"), *GetNameSafe(Interaction));
    }
}

void UVRWidgetInputComponent::ReleaseAllPointers()
{
    Release(false);
    Release(true);
}

void UVRWidgetInputComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    TryBindInput();
    bool bUseFocus = false, bHasFocus = true;
    UHeadMountedDisplayFunctionLibrary::GetVRFocusState(bUseFocus, bHasFocus);
    if (bUseFocus && !bHasFocus) ReleaseAllPointers();
    AActor* Menu = Cast<AActor>(UCNAReliabilityLibrary::ObjectProperty(GetOwner(), TEXT("MenuReference")));
    // A destroyed weak menu reference resolves to null before this tick. Keep its
    // previous open state so closing still releases a press held on a pawn laser.
    const bool bMenuOpen = IsValid(Menu);
    const FBoolProperty* HandProperty = bMenuOpen ? FindFProperty<FBoolProperty>(Menu->GetClass(), TEXT("bActiveMenuHandRight")) : nullptr;
    const bool bMenuHandRight = HandProperty && HandProperty->GetPropertyValue_InContainer(Menu);
    if (bMenuWasOpen != bMenuOpen || LastMenu.Get() != Menu || (bMenuOpen && bLastMenuHandRight != bMenuHandRight)) ReleaseAllPointers();
    if (bMenuOpen && LastMenu.Get() != Menu)
    {
        UWidgetInteractionComponent* Pointer = ResolveInteraction(bMenuHandRight);
        UE_LOG(LogCNAWidgetInput, Display, TEXT("Menu opened actor=%s selectedHand=%s cursorPointer=%s parent=%s cachedLeft=%s cachedRight=%s"),
            *GetNameSafe(Menu), bMenuHandRight ? TEXT("right") : TEXT("left"), *GetNameSafe(Pointer),
            *GetNameSafe(Pointer ? Pointer->GetAttachParent() : nullptr),
            *GetNameSafe(UCNAReliabilityLibrary::ObjectProperty(Menu, TEXT("WidgetInteractionRefLeft"))),
            *GetNameSafe(UCNAReliabilityLibrary::ObjectProperty(Menu, TEXT("WidgetInteractionRefRight"))));
    }
    if (bMenuOpen)
    {
        FVector2D LeftStick = FVector2D::ZeroVector, RightStick = FVector2D::ZeroVector;
        APawn* Pawn = Cast<APawn>(GetOwner());
        APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
        UEnhancedPlayerInput* Input = PC ? Cast<UEnhancedPlayerInput>(PC->PlayerInput) : nullptr;
        if (!bSuspended && (!bUseFocus || bHasFocus) && Input)
        {
            if (CursorLeftAction) LeftStick = Input->GetActionValue(CursorLeftAction).Get<FVector2D>();
            if (CursorRightAction) RightStick = Input->GetActionValue(CursorRightAction).Get<FVector2D>();
        }
        UpdateMenuCursorFromHands(Menu, LeftStick, RightStick, DeltaTime);
    }
    else if (CursorMenu.IsValid() || MenuPointer.IsValid()) ClearMenuCursor();
    bMenuWasOpen = bMenuOpen;
    bLastMenuHandRight = bMenuHandRight;
    LastMenu = Menu;
    for (int32 Hand = 0; Hand != 2; ++Hand)
        if (PressedPointers[Hand].IsValid() && (PressedPointers[Hand].Get() != ResolveInteraction(Hand == 1) || !PressedPointers[Hand]->IsActive())) Release(Hand == 1);
}


void UVRWidgetInputComponent::ClearMenuCursor()
{
    ReleaseAllPointers();
    if (MenuPointer.IsValid()) MenuPointer->DestroyComponent();
    MenuPointer.Reset(); MenuWidget.Reset(); MenuDot.Reset(); CursorMenu.Reset();
    bMenuClosing = false;
}

void UVRWidgetInputComponent::StopMenuInteraction(AActor* Menu)
{
    if (CursorMenu.Get() != Menu) return;
    ReleaseAllPointers();
    bMenuClosing = true;
    if (MenuPointer.IsValid()) MenuPointer->UpdateWidgetHit(nullptr, MenuCursorPosition);
    if (MenuDot.IsValid()) MenuDot->SetVisibility(false);
}

void UVRWidgetInputComponent::SetMenuCursorPosition(FVector2D Position)
{
    MenuCursorPosition = FVector2D(FMath::Clamp(Position.X, 0.001, 0.999), FMath::Clamp(Position.Y, 0.001, 0.999));
    if (CursorMenu.IsValid()) UpdateMenuCursor(CursorMenu.Get(), FVector2D::ZeroVector, 0.f);
}

void UVRWidgetInputComponent::UpdateMenuCursorFromHands(AActor* Menu, FVector2D LeftStick, FVector2D RightStick, float DeltaTime)
{
    UpdateMenuCursor(Menu, IsRightHandMenu(Menu) ? RightStick : LeftStick, DeltaTime);
}

void UVRWidgetInputComponent::UpdateMenuCursor(AActor* Menu, FVector2D Stick, float DeltaTime)
{
    if (!IsValid(Menu)) return;
    if (CursorMenu.Get() != Menu)
    {
        ClearMenuCursor();
        CursorMenu = Menu;
        MenuWidget = Cast<UWidgetComponent>(UCNAReliabilityLibrary::ObjectProperty(Menu, TEXT("Widget")));
        MenuDot = Cast<USceneComponent>(UCNAReliabilityLibrary::ObjectProperty(Menu, TEXT("Cursor")));
        MenuCursorPosition = FVector2D(0.5, 0.5);
        bMenuHoverLogged = false;
        if (!MenuWidget.IsValid() || !MenuDot.IsValid()) { UE_LOG(LogCNAWidgetInput, Error, TEXT("Menu widget/cursor missing")); return; }
        UMenuCursorInteraction* Pointer = NewObject<UMenuCursorInteraction>(Menu, TEXT("StickCursorInteraction"));
        Menu->AddInstanceComponent(Pointer);
        Pointer->SetupAttachment(MenuWidget.Get());
        Pointer->RegisterComponent();
        Pointer->SetComponentTickEnabled(false);
        MenuPointer = Pointer;
        // The dot always starts on the menu, without a ray hit or either stick moving.
        MenuDot->SetHiddenInGame(false);
        MenuDot->SetVisibility(true);
        UE_LOG(LogCNAWidgetInput, Display, TEXT("Dedicated menu dot initialized: pointer=%s index=2 controllingHand=%s"), *Pointer->GetName(), IsRightHandMenu(Menu) ? TEXT("right") : TEXT("left"));
    }
    if (bMenuClosing || !MenuWidget.IsValid() || !MenuDot.IsValid() || !MenuPointer.IsValid()) return;
    FVector2D Size = MenuWidget->GetCurrentDrawSize();
    if (Size.X <= 0 || Size.Y <= 0) Size = MenuWidget->GetDrawSize();
    // Pixel speed remains consistent if the menu's desired render size changes.
    MenuCursorPosition.X = FMath::Clamp(MenuCursorPosition.X + Stick.X * 400.f * DeltaTime / FMath::Max(Size.X, 1.), 0.001, 0.999);
    MenuCursorPosition.Y = FMath::Clamp(MenuCursorPosition.Y - Stick.Y * 400.f * DeltaTime / FMath::Max(Size.Y, 1.), 0.001, 0.999);
    const FVector Position = UMenuCursorInteraction::PixelToWorld(MenuWidget.Get(), MenuCursorPosition * Size, Size);
    MenuDot->SetWorldLocation(Position + MenuWidget->GetForwardVector());
    MenuDot->SetVisibility(true);
    MenuPointer->UpdateWidgetHit(MenuWidget.Get(), MenuCursorPosition);
    if (!bMenuHoverLogged && MenuPointer->IsOverInteractableWidget())
    {
        bMenuHoverLogged = true;
        UE_LOG(LogCNAWidgetInput, Display, TEXT("Menu dot hit testing ready widget=%s pixel=(%.1f,%.1f) size=(%.1f,%.1f)"),
            *GetNameSafe(MenuPointer->GetHoveredWidgetComponent()), MenuPointer->Get2DHitLocation().X, MenuPointer->Get2DHitLocation().Y, Size.X, Size.Y);
    }
}

void UVRWidgetInputComponent::Suspend() { bSuspended = true; ReleaseAllPointers(); UE_LOG(LogCNAWidgetInput, Display, TEXT("Widget input suspended by app focus")); }
void UVRWidgetInputComponent::Resume() { bSuspended = false; UE_LOG(LogCNAWidgetInput, Display, TEXT("Widget input resumed")); }

void UVRWidgetInputComponent::UnbindInput()
{
    ReleaseAllPointers();
    if (BoundInput.IsValid()) for (uint32 Handle : BindingHandles) BoundInput->RemoveBindingByHandle(Handle);
    BindingHandles.Reset();
    BoundInput.Reset();
    if (InputSubsystem.IsValid() && WidgetContext.IsValid()) InputSubsystem->RemoveMappingContext(WidgetContext.Get());
    InputSubsystem.Reset();
}

void UVRWidgetInputComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    UnbindInput();
    ClearMenuCursor();
    FCoreDelegates::ApplicationWillDeactivateDelegate.Remove(DeactivateHandle);
    FCoreDelegates::ApplicationWillEnterBackgroundDelegate.Remove(BackgroundHandle);
    FCoreDelegates::ApplicationHasReactivatedDelegate.Remove(ReactivateHandle);
    FCoreDelegates::ApplicationHasEnteredForegroundDelegate.Remove(ForegroundHandle);
    Super::EndPlay(Reason);
}
