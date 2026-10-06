#pragma once

#include "Components/ActorComponent.h"
#include "VRWidgetInputComponent.generated.h"

class UInputMappingContext;
class UInputAction;
class UEnhancedInputComponent;
class UEnhancedInputLocalPlayerSubsystem;
class UWidgetInteractionComponent;
class UMenuCursorInteraction;
class UWidgetComponent;
class USceneComponent;

/** Pawn-owned click routing for both menu and training widgets. */
UCLASS(ClassGroup=(CNA), meta=(BlueprintSpawnableComponent))
class HANDLINGRAGDOLLS_API UVRWidgetInputComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UVRWidgetInputComponent();
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    UFUNCTION(BlueprintCallable, Category="CNA|UI") void ReleaseAllPointers();
    static UWidgetInteractionComponent* ResolveInteractionForPawn(AActor* Pawn, bool bRight);
    void StopMenuInteraction(AActor* Menu);
    void UpdateMenuCursor(AActor* Menu, FVector2D Stick, float DeltaTime);
    void SetMenuCursorPosition(FVector2D Position);
    void ClickLeftForTest() { Press(false); Release(false); }
    void ClickRightForTest() { Press(true); Release(true); }
    void UpdateMenuCursorFromHands(AActor* Menu, FVector2D LeftStick, FVector2D RightStick, float DeltaTime);
private:
    UPROPERTY(EditDefaultsOnly, Category="CNA|UI") TSoftObjectPtr<UInputMappingContext> WidgetContext;
    UPROPERTY(EditDefaultsOnly, Category="CNA|UI") TSoftObjectPtr<UInputAction> LeftAction;
    UPROPERTY(EditDefaultsOnly, Category="CNA|UI") TSoftObjectPtr<UInputAction> RightAction;
    TWeakObjectPtr<UEnhancedInputComponent> BoundInput;
    TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> InputSubsystem;
    TWeakObjectPtr<UWidgetInteractionComponent> PressedPointers[2];
    TWeakObjectPtr<AActor> LastMenu;
    UPROPERTY() TObjectPtr<UInputAction> CursorLeftAction;
    UPROPERTY() TObjectPtr<UInputAction> CursorRightAction;
    TWeakObjectPtr<AActor> CursorMenu;
    TWeakObjectPtr<UMenuCursorInteraction> MenuPointer;
    TWeakObjectPtr<UWidgetComponent> MenuWidget;
    TWeakObjectPtr<USceneComponent> MenuDot;
    FVector2D MenuCursorPosition = FVector2D(0.5, 0.5);
    bool bMenuClosing = false;
    bool bMenuHoverLogged = false;
    void ClearMenuCursor();
    TArray<uint32> BindingHandles;
    bool bSuspended = false;
    bool bMenuWasOpen = false;
    bool bLastMenuHandRight = false;
    FDelegateHandle DeactivateHandle;
    FDelegateHandle BackgroundHandle;
    FDelegateHandle ReactivateHandle;
    FDelegateHandle ForegroundHandle;
    bool TryBindInput();
    void UnbindInput();
    UWidgetInteractionComponent* ResolveInteraction(bool bRight) const;
    void Press(bool bRight);
    void Release(bool bRight);
    void LeftPressed() { Press(false); }
    void RightPressed() { Press(true); }
    void LeftReleased() { Release(false); }
    void RightReleased() { Release(true); }
    void Suspend();
    void Resume();
};
