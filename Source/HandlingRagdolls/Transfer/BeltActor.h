// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "../Interfaces/IGrabbable.h"
#include "BeltActor.generated.h"

class UBeltComponent;
class UStaticMeshComponent;
class USceneComponent;
class USphereComponent;

/**
 * Belt Actor — the physical transfer belt that the nurse attaches to the patient.
 * 
 * This actor owns the BeltComponent and implements IGrabbable at the actor level.
 * (UActorComponents cannot safely implement UInterfaces in UE5 reflection.)
 * 
 * Workflow:
 * 1. Nurse picks up belt (IGrabbable)
 * 2. Nurse brings belt near patient → auto-attaches via overlap detection
 * 3. Once attached, belt handles become the grab targets for lifting
 */
UCLASS(BlueprintType)
class HANDLINGRAGDOLLS_API ABeltActor : public AActor, public IIGrabbable
{
	GENERATED_BODY()

public:
	ABeltActor();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

public:
	// ============================================================
	// IGrabbable Implementation
	// ============================================================
	virtual bool CanBeGrabbed(FName BoneName, FVector GrabLocation) const override;
	virtual void OnGrabbed(UGrabComponent* Grabber, FName BoneName, FVector GrabLocation) override;
	virtual void OnReleased(UGrabComponent* Grabber) override;
	virtual UPrimitiveComponent* GetGrabbableComponent() const override;
	virtual TArray<FName> GetGrabbableBoneNames() const override;
	virtual FName GetGrabBoneOverride() const override;
	virtual bool RequiresRotationConstraint() const override;

	/** Get the belt component */
	UFUNCTION(BlueprintCallable, Category = "Belt")
	UBeltComponent* GetBeltComponent() const { return BeltComp; }

protected:
	/** The belt mesh */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Belt")
	TObjectPtr<UStaticMeshComponent> BeltMesh;

	/** Front handle grab point */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Belt")
	TObjectPtr<USceneComponent> HandleFront;

	/** Front handle visual indicator (sphere) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Belt")
	TObjectPtr<UStaticMeshComponent> HandleFrontVisual;

	/** Offset of the front handle from the belt center (tweak to position it in front of the patient) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Belt|Config")
	FVector HandleOffset = FVector(40.0f, 0.0f, 0.0f);

	/** The belt logic component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Belt")
	TObjectPtr<UBeltComponent> BeltComp;

	/** Proximity detection sphere — visual reference for attach range */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Belt")
	TObjectPtr<USphereComponent> AttachProximity;

	/** Distance (in cm) at which the belt auto-attaches to the patient while being carried */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Belt|Detection",
		meta = (ClampMin = "10.0", ClampMax = "500.0"))
	float AttachRadius = 75.0f;

	/** Show the detection sphere in-game for debugging attachment range */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Belt|Detection")
	bool bShowDetectionRadius = false;

private:
	/** Whether the belt is currently being held by the nurse */
	bool bIsBeingCarried = false;
};
