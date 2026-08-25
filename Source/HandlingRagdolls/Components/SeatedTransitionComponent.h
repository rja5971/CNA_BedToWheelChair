// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "../Patient/PatientBoneMapping.h"
#include "SeatedTransitionComponent.generated.h"

class UAnimSequence;
class USkeletalMeshComponent;
class UPatientPhysicsComponent;
class UAnimInstance;
class AWheelchairActor;

/** Broadcast when the physics-to-animation handoff has completed. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSeatedReached);

/** Handles the bed settle and the final chair animation handoff. */
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

	/** Angle of the torso from vertical (lying ~= 90, upright ~= 0). */
	UFUNCTION(BlueprintCallable, Category = "Seated Transition")
	float GetTorsoUprightAngleDeg() const;

	/** Returns true on the frame the patient first crosses the seated threshold. */
	bool CheckSitThreshold(float TorsoAngle);

	/** Start a blend at the patient's current world position. */
	UFUNCTION(BlueprintCallable, Category = "Seated Transition")
	void BeginSeatedSettle();

	/** Immediately hand animation control to the chair pose and align its pelvis to the seat target. */
	UFUNCTION(BlueprintCallable, Category = "Seated Transition")
	void BeginSeatedBlendToTarget(const FTransform& SeatTarget, AWheelchairActor* Wheelchair = nullptr);

	/** Chair supplying the active footrest and knee IK markers. */
	UFUNCTION(BlueprintPure, Category = "Seated Transition|IK")
	AWheelchairActor* GetTargetWheelchair() const { return TargetWheelchair.Get(); }

	/** Legacy/manual escape hatch for already seated actors. */
	UFUNCTION(BlueprintCallable, Category = "Seated Transition")
	void MarkSeated();

	/** Re-arm the component after the patient leaves a seated state. */
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

	/** Animation that becomes the final seated idle pose. Must use the patient's skeleton. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Seated Transition|Animation")
	TObjectPtr<UAnimSequence> SeatedAnimation;

	/** Optional seated Anim Blueprint. If unset, seating uses the legacy single-node animation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Seated Transition|Animation")
	TSubclassOf<UAnimInstance> SeatedAnimClass;

	/**
	 * Enables the experimental foot-target Anim Blueprint path. Disabled by
	 * default so the validated single-node seated animation remains active.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Seated Transition|Animation")
	bool bEnableSeatedFootIK = false;

	/** Total time used to hand control from physics to animation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Seated Transition|Animation", meta = (ClampMin = "0.1"))
	float BlendDuration = 1.5f;

	/** Begin looping the seated animation during the transition. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Seated Transition|Animation")
	bool bLoopSeatedAnimation = true;

	/** Arms, legs and head retain physics slightly longer than the pelvis/torso. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Seated Transition|Animation", meta = (ClampMin = "0.0", ClampMax = "0.75"))
	float LimbBlendDelay = 0.18f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Seated Transition|Detection", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float SitUprightAngleThreshold = 45.0f;

	/** Grace angle allowed while the early, mostly-physical part of the blend runs. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Seated Transition|Detection", meta = (ClampMin = "0.0", ClampMax = "45.0"))
	float CancelAngleGrace = 20.0f;

	/** Maximum final translation used to place the animated pelvis on the seat target. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Seated Transition|Alignment", meta = (ClampMin = "0.0"))
	float MaxSeatPositionCorrection = 30.0f;

	/** Maximum final yaw correction toward the chair orientation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Seated Transition|Alignment", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float MaxSeatYawCorrection = 45.0f;

private:
	void StartBlend(const FTransform* SeatTarget);
	void SnapToAnimationAtTarget(const FTransform& SeatTarget);
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
	TWeakObjectPtr<AWheelchairActor> TargetWheelchair;

	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> Mesh;

	UPROPERTY()
	TObjectPtr<UPatientBoneMapping> BoneMapping;

	UPROPERTY()
	TObjectPtr<UPatientPhysicsComponent> PhysicsComp;
};
