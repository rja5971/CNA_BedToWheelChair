// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IBeltAttachable.generated.h"

class UBeltComponent;

/**
 * Interface for actors that can receive a transfer belt attachment.
 * Separates belt-specific behavior from general grab behavior (ISP).
 */
UINTERFACE(MinimalAPI, Blueprintable)
class UIBeltAttachable : public UInterface
{
	GENERATED_BODY()
};

class HANDLINGRAGDOLLS_API IIBeltAttachable
{
	GENERATED_BODY()

public:
	/** Check if a belt can currently be attached (e.g., patient must be in correct position) */
	virtual bool CanAttachBelt() const = 0;

	/** Get the transform where the belt should attach (around the torso) */
	virtual FTransform GetBeltAttachTransform() const = 0;

	/** Get the bone name the belt should attach to */
	virtual FName GetBeltAttachBoneName() const = 0;

	/** Called when belt is successfully attached */
	virtual void OnBeltAttached(UBeltComponent* Belt) = 0;

	/** Called when belt is removed */
	virtual void OnBeltDetached(UBeltComponent* Belt) = 0;

	/** Returns true if a belt is currently attached */
	virtual bool HasBeltAttached() const = 0;
};
