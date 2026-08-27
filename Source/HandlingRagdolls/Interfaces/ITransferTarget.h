// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ITransferTarget.generated.h"

/**
 * Interface for transfer destinations (wheelchair, bed, chair, etc.).
 * Open/Closed: new target types can be added without modifying existing code.
 * Liskov: any ITransferTarget can be used interchangeably by the state machine.
 */
UINTERFACE(MinimalAPI, Blueprintable)
class UITransferTarget : public UInterface
{
	GENERATED_BODY()
};

class HANDLINGRAGDOLLS_API IITransferTarget
{
	GENERATED_BODY()

public:
	/** Check if the target is ready to receive a patient (positioned, unlocked, etc.) */
	virtual bool IsReadyToReceive() const = 0;

	/** Get the transform where the patient should end up seated/positioned */
	virtual FTransform GetTargetSeatTransform() const = 0;

	/** Get the acceptable radius for patient placement (how close is close enough) */
	virtual float GetAcceptanceRadius() const = 0;

	/** Called when a transfer to this target begins */
	virtual void OnTransferBegin(AActor* Patient) = 0;

	/** Called when transfer completes successfully */
	virtual void OnTransferComplete(AActor* Patient) = 0;

	/** Called if transfer is aborted/failed */
	virtual void OnTransferFailed(AActor* Patient) = 0;
};
