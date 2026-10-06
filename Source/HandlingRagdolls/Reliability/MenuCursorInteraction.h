#pragma once

#include "Components/WidgetInteractionComponent.h"
#include "MenuCursorInteraction.generated.h"

class UWidgetComponent;

/** A dot on a known menu plane; controller rays cannot change its hit target. */
UCLASS()
class HANDLINGRAGDOLLS_API UMenuCursorInteraction : public UWidgetInteractionComponent
{
    GENERATED_BODY()
public:
    UMenuCursorInteraction();
    void UpdateWidgetHit(UWidgetComponent* Widget, FVector2D NormalizedPosition);
    static FVector PixelToWorld(const UWidgetComponent* Widget, FVector2D Pixel, FVector2D Size);
    // Runtime smoke tests observe real Slate button clicks through these delegates.
    UFUNCTION() void ObserveReset() { ++ResetClicks; }
    UFUNCTION() void ObserveRestart() { ++RestartClicks; }
    UFUNCTION() void ObserveExit() { ++ExitClicks; }
    int32 ResetClicks = 0, RestartClicks = 0, ExitClicks = 0;
};
