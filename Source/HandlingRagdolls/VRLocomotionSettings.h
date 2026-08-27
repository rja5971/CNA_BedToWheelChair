#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VRLocomotionSettings.generated.h"

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class UVRLocomotionSettings : public UActorComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UVRLocomotionSettings();

	// --- SETTINGS EXPOSED TO BLUEPRINTS ---

	// True for Snap Turn, False for Continuous Turn
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR Locomotion")
	bool bUseSnapTurn;

	// True for Teleport, False for Continuous Movement
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR Locomotion")
	bool bUseTeleport;

	// How many degrees to turn per second in Continuous Turn mode
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR Locomotion")
	float ContinuousTurnSpeed;

	// Multiplier for smooth movement speed
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR Locomotion")
	float MovementSpeedMultiplier;

	// --- FUNCTIONS TO USE IN BLUEPRINTS ---

	/*
	 * Call this from your IA_Turn Input Action.
	 * It automatically handles either Snap Turning (if bUseSnapTurn is true)
	 * or Continuous Turning (if false).
	 */
	UFUNCTION(BlueprintCallable, Category = "VR Locomotion")
	void ApplyTurn(APawn* Pawn, float AxisValue, float DeltaTime);

	/*
	 * Call this from your IA_Move Input Action.
	 * It handles smooth movement. (If teleport is true, you'll bypass this in Blueprints).
	 */
	UFUNCTION(BlueprintCallable, Category = "VR Locomotion")
	void ApplySmoothMove(APawn* Pawn, FVector2D AxisValue, FVector ForwardDirection, FVector RightDirection);

private:
	// Internal variable to prevent spamming snap turns
	bool bHasSnapTurned;
};