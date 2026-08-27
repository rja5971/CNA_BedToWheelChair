#include "VRLocomotionSettings.h"
#include "GameFramework/Pawn.h"

// Sets default values
UVRLocomotionSettings::UVRLocomotionSettings()
{
	// We don't need this component to tick every frame
	PrimaryComponentTick.bCanEverTick = false;

	// Default Settings
	bUseSnapTurn = true;
	bUseTeleport = true;
	ContinuousTurnSpeed = 45.0f;
	MovementSpeedMultiplier = 0.5f; // Default to half speed, since they want it decreased
	bHasSnapTurned = false;
}

void UVRLocomotionSettings::ApplyTurn(APawn* Pawn, float AxisValue, float DeltaTime)
{
	if (!Pawn) return;

	// SNAP TURN LOGIC
	if (bUseSnapTurn)
	{
		// Deadzone check: only turn if stick is pushed far enough
		if (FMath::Abs(AxisValue) > 0.5f)
		{
			if (!bHasSnapTurned)
			{
				float SnapAngle = (AxisValue > 0.0f) ? 45.0f : -45.0f;
				Pawn->AddActorLocalRotation(FRotator(0.0f, SnapAngle, 0.0f));
				bHasSnapTurned = true; // Lock turning until stick returns to center
			}
		}
		else
		{
			// Reset the lock when stick goes back to the center
			bHasSnapTurned = false;
		}
	}
	// CONTINUOUS TURN LOGIC
	else
	{
		float TurnAmount = AxisValue * ContinuousTurnSpeed * DeltaTime;
		Pawn->AddActorLocalRotation(FRotator(0.0f, TurnAmount, 0.0f));
	}
}

void UVRLocomotionSettings::ApplySmoothMove(APawn* Pawn, FVector2D AxisValue, FVector ForwardDirection, FVector RightDirection)
{
	if (!Pawn || bUseTeleport) return; // Don't apply smooth move if teleport is active

	// Add forward/backward movement
	Pawn->AddMovementInput(ForwardDirection, AxisValue.Y * MovementSpeedMultiplier);

	// Add left/right movement
	Pawn->AddMovementInput(RightDirection, AxisValue.X * MovementSpeedMultiplier);
}