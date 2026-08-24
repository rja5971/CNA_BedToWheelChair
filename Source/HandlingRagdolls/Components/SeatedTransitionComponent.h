// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "../Patient/PatientBoneMapping.h"
#include "SeatedTransitionComponent.generated.h"

class UAnimSequence;
class USkeletalMeshComponent;
class UPatientPhysicsComponent;

/** Broadcast when the physics-to-animation handoff has completed. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSeatedReached);

/** Smoothly hands the patient from a simulated ragdoll to a seated animation. */
UCLASS(ClassGroup = (PatientCare), meta = (BlueprintSpawnableComponent))
class HANDLINGRAGDOLLS_API USeatedTransitionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USeatedTransitionComponent();

protected:
	virtual void BeginPlay() override;

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	void Initialize(USkeletalMeshComponent* InMesh, UPatientBoneMapping* InBoneMapping,
		UPatientPhysicsComponent* InPhysics);

	UFUNCTION(BlueprintCallable, Category = "Seated Transition")
	float GetTorsoUprightAngleDeg() const;

	bool CheckSitThreshold(float TorsoAngle);

	/** Start a blend at the patient's current world position. */
	UFUNCTION(BlueprintCallable, Category = "Seated Transition")
	void BeginSeatedSettle();

	/** Start a blend and align the final animated pelvis to a chair's seat target. */
	UFUNCTION(BlueprintCallable, Category = "Seated Transition")
	void BeginSeatedBlendToTarget(const FTransform& SeatTarget);

	UFUNCTION(BlueprintCallable, Category = "Seated Transition")
	void MarkSeated();

	/** Re-arm after the patient leaves a seated state. */
	UFUNCTION(BlueprintCallable, Category = "Seated Transition")
	void ResetTransition();

	UFUNCTION(BlueprintCallable, Category = "Seated Transition")
	bool IsSeatedLocked() const { return bSeatedLocked; }

	UFUNCTION(BlueprintCallable, Category = "Seated Transition")
	bool IsSettling() const { return bBlending; }

	bool IsBlendingToSeated() const { return bBlending; }

	UPROPERTY(BlueprintAssignable, Category = "Seated Transition|Events")
	FOnSeatedReached OnSeatedReached;

	UPROPERTY(BlueprintAssignable, Category = "Seated Transition|Events")
	FOnSeatedReached OnSettleCancelled;

	/** The exact Sitting Idle FBX imported onto the elderly-patient skeleton. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Seated Transition|Animation")
	TObjectPtr<UAnimSequence> SeatedAnimation;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Seated Transition|Animation", meta = (ClampMin = "0.1"))
	float BlendDuration = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Seated Transition|Animation")
	bool bLoopSeatedAnimation = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Seated Transition|Animation", meta = (ClampMin = "0.0", ClampMax = "0.75"))
	float LimbBlendDelay = 0.18f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Seated Transition|Detection", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float SitUprightAngleThreshold = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Seated Transition|Detection", meta = (ClampMin = "0.0", ClampMax = "45.0"))
	float CancelAngleGrace = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Seated Transition|Alignment", meta = (ClampMin = "0.0"))
	float MaxSeatPositionCorrection = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Seated Transition|Alignment", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float MaxSeatYawCorrection = 45.0f;

private:
	void StartBlend(const FTransform* SeatTarget);
	void ApplyPhysicsBlend(float TorsoAlpha, float LimbAlpha);
	void CompleteBlend();
	void AlignAnimationToSeatTarget();
	void CancelBlend();
	FName ResolveBoneName(EPatientBoneRole BoneRole) const;

	bool bSeatedLocked = false;
	bool bBlending = false;
	bool bHasSeatTarget = false;
	float BlendElapsed = 0.0f;
	FTransform TargetSeatTransform = FTransform::Identity;

	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> Mesh;

	UPROPERTY()
	TObjectPtr<UPatientBoneMapping> BoneMapping;

	UPROPERTY()
	TObjectPtr<UPatientPhysicsComponent> PhysicsComp;
};
