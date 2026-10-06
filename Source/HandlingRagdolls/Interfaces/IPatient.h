// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "../Patient/PatientTypes.h"
#include "IPatient.generated.h"

class USkeletalMeshComponent;

/**
 * Interface for querying and controlling patient state.
 * Dependency Inversion: state machine and transfer systems depend on this abstraction,
 * not on APatientActor directly.
 */
UINTERFACE(MinimalAPI, Blueprintable)
class UIPatient : public UInterface
{
	GENERATED_BODY()
};

class HANDLINGRAGDOLLS_API IIPatient
{
	GENERATED_BODY()

public:
	/** Check if neck is currently being supported */
	virtual bool IsNeckSupported() const = 0;

	/** Get world location of the patient's pelvis bone */
	virtual FVector GetPelvisLocation() const = 0;

	/** Get physics linear velocity of the patient's pelvis bone */
	virtual FVector GetPelvisVelocity() const = 0;

	/** Check if a belt is currently attached */
	virtual bool HasBeltAttached() const = 0;

	/** Set the patient's current state */
	virtual void SetPatientState(EPatientState NewState) = 0;

	/** Get the patient's current state */
	virtual EPatientState GetPatientState() const = 0;

	/** Get the patient's skeletal mesh component */
	virtual USkeletalMeshComponent* GetPatientMesh() const = 0;

	/** Get overall spine stress as a 0-1 value */
	virtual float GetSpineStressLevel() const = 0;
};
