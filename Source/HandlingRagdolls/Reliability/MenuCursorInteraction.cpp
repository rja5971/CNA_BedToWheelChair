#include "MenuCursorInteraction.h"
#include "Components/WidgetComponent.h"

UMenuCursorInteraction::UMenuCursorInteraction()
{
    VirtualUserIndex = 1;
    PointerIndex = 2;
    InteractionSource = EWidgetInteractionSource::Custom;
    bEnableHitTesting = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
}

FVector UMenuCursorInteraction::PixelToWorld(const UWidgetComponent* Widget, FVector2D Pixel, FVector2D Size)
{
    const FVector2D Pivot = Widget->GetPivot();
    return Widget->GetComponentTransform().TransformPosition(FVector(0, Pivot.X * Size.X - Pixel.X, Pivot.Y * Size.Y - Pixel.Y));
}

void UMenuCursorInteraction::UpdateWidgetHit(UWidgetComponent* Widget, FVector2D NormalizedPosition)
{
    const FVector2D Size = IsValid(Widget) ? Widget->GetCurrentDrawSize() : FVector2D::ZeroVector;
    FHitResult Hit;
    if (IsValid(Widget) && Widget->IsVisible() && Size.X > 0 && Size.Y > 0 && !Widget->GetComponentScale().IsNearlyZero())
    {
        const FVector Position = PixelToWorld(Widget, NormalizedPosition * Size, Size);
        const FVector Normal = Widget->GetForwardVector();
        Hit = FHitResult(Widget->GetOwner(), Widget, Position, Normal);
        Hit.bBlockingHit = true;
        Hit.Location = Hit.ImpactPoint = Position;
        Hit.TraceStart = Position + Normal * 10;
        Hit.TraceEnd = Position - Normal * 10;
    }
    SetCustomHitResult(Hit);
    SimulatePointerMovement();
}
