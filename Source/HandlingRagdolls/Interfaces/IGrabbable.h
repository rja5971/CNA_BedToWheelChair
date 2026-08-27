// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IGrabbable.generated.h"

class UGrabComponent;

/**
 * Interface for any actor/component that can be physically grabbed by VR hands.
 * Implements Single Responsibility: only defines grab contract.
 * Implements Interface Segregation: focused solely on grab behavior.
 */
UINTERFACE(MinimalAPI, Blueprintable)
class UIGrabbable : public UInterface
{
	GENERATED_BODY()
};

class HANDLINGRAGDOLLS_API IIGrabbable
{
	GENERATED_BODY()

public:
	/** Check if this object can be grabbed at the specified bone/location */
	virtual bool CanBeGrabbed(FName BoneName, FVector GrabLocation) const = 0;

	/** Called when a grab begins */
	virtual void OnGrabbed(UGrabComponent* Grabber, FName BoneName, FVector GrabLocation) = 0;

	/** Called when a grab ends */
	virtual void OnReleased(UGrabComponent* Grabber) = 0;

	/** Returns the primitive component that should be used for physics grab */
	virtual UPrimitiveComponent* GetGrabbableComponent() const = 0;

	/** Returns all valid bone names that can be grabbed on this object */
	virtual TArray<FName> GetGrabbableBoneNames() const = 0;

	/**
	 * Optional: override which bone the physics handle should grab.
	 * Return NAME_None (default) to use the bone from the grab trace.
	 * Used e.g. by the belt to redirect the grab to the patient's spine bone.
	 */
	virtual FName GetGrabBoneOverride() const { return NAME_None; }

	/**
	 * Optional: whether the grab should constrain rotation (like a solid object) 
	 * or just location (like a heavy ragdoll or pendulum).
	 */
	virtual bool RequiresRotationConstraint() const { return true; }

	/** Kinematic interactions can receive grab events without creating a physics handle. */
	virtual bool ShouldUsePhysicsHandle() const { return true; }
};
