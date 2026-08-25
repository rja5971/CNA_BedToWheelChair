#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PatientCarryComponent.generated.h"

class ABeltActor;
class UAnimSequence;
class UGrabComponent;
class UPatientPhysicsComponent;
class USkeletalMeshComponent;

/** Animation-owned belt carry: follows the held belt and yaw-billboards toward the VR viewer. */
UCLASS(ClassGroup = (PatientCare), meta = (BlueprintSpawnableComponent))
class HANDLINGRAGDOLLS_API UPatientCarryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPatientCarryComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	void Initialize(USkeletalMeshComponent* InMesh, UPatientPhysicsComponent* InPhysics);
	bool BeginCarry(UGrabComponent* Grabber, ABeltActor* BeltActor);
	void EndCarry(UGrabComponent* Grabber);
	void PrepareForSeating();

	UFUNCTION(BlueprintPure, Category = "Patient|Kinematic Carry")
	bool IsCarryActive() const { return bCarryActive || bReleasePending; }

	UFUNCTION(BlueprintPure, Category = "Patient|Kinematic Carry")
	bool CanUseKinematicCarry() const { return CarryAnimation != nullptr; }

	/** Upright, in-place, looping animation that owns the entire body while carried. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Patient|Kinematic Carry|Animation")
	TObjectPtr<UAnimSequence> CarryAnimation;

	/** Character-forward calibration added to the yaw that faces the headset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patient|Kinematic Carry|Facing")
	float FacingYawOffset = 0.0f;

	/** Maximum billboard turn rate in degrees per second. Zero snaps immediately. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patient|Kinematic Carry|Facing", meta = (ClampMin = "0.0"))
	float FacingTurnSpeed = 540.0f;

	/** Pitch used for the upright animated mesh. Kept configurable for imported mesh-axis differences. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patient|Kinematic Carry|Facing")
	float UprightPitch = 0.0f;

	/** Roll used for the upright animated mesh. Kept configurable for imported mesh-axis differences. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patient|Kinematic Carry|Facing")
	float UprightRoll = 0.0f;

	/** Do not recompute facing when the headset is almost directly above the patient. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patient|Kinematic Carry|Facing", meta = (ClampMin = "1.0"))
	float MinimumFacingDistance = 15.0f;

	/** Translation smoothing speed. Zero aligns the belt handle to the hand exactly each frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patient|Kinematic Carry|Follow", meta = (ClampMin = "0.0"))
	float FollowInterpSpeed = 30.0f;

	/** Optional world-space adjustment between the averaged hands and the belt handle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patient|Kinematic Carry|Follow")
	FVector HandAnchorOffset = FVector::ZeroVector;

	/** Time allowed for an accidental release before physics is restored. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patient|Kinematic Carry|Release", meta = (ClampMin = "0.0"))
	float ReleaseGracePeriod = 0.35f;

private:
	bool GetHandAnchor(FVector& OutAnchor) const;
	bool GetViewerLocation(FVector& OutLocation) const;
	void RestorePhysicsAfterRelease();

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> Mesh;

	UPROPERTY(Transient)
	TObjectPtr<UPatientPhysicsComponent> PhysicsComp;

	UPROPERTY(Transient)
	TObjectPtr<ABeltActor> ActiveBelt;

	UPROPERTY(Transient)
	TObjectPtr<AActor> ViewerActor;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UGrabComponent>> ActiveGrabbers;

	bool bCarryActive = false;
	bool bReleasePending = false;
	float ReleaseElapsed = 0.0f;
	float LastFacingYaw = 0.0f;
};
