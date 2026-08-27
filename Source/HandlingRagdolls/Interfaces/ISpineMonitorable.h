// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ISpineMonitorable.generated.h"

/**
 * Interface for querying spine health/state of a patient.
 * Dependency Inversion: scoring/feedback systems depend on this abstraction,
 * not on PatientActor directly.
 */
UINTERFACE(MinimalAPI, Blueprintable)
class UISpineMonitorable : public UInterface
{
	GENERATED_BODY()
};

class HANDLINGRAGDOLLS_API IISpineMonitorable
{
	GENERATED_BODY()

public:
	/** Get overall spine stress as a 0-1 value (0 = no stress, 1 = critical) */
	virtual float GetSpineStressLevel() const = 0;

	/** Get stress level for a specific spine bone */
	virtual float GetBoneStressLevel(FName BoneName) const = 0;

	/** Check if spine damage threshold has been exceeded (simulation failure) */
	virtual bool IsSpineDamaged() const = 0;

	/** Get list of bones currently under dangerous stress */
	virtual TArray<FName> GetStressedBones() const = 0;

	/** Get the safe angular limit for a given bone (in degrees) */
	virtual float GetSafeAngleLimit(FName BoneName) const = 0;

	/** Get current angular deviation from neutral for a bone */
	virtual float GetCurrentAngleDeviation(FName BoneName) const = 0;
};
