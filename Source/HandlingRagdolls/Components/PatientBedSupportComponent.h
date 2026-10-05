#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PatientBedSupportComponent.generated.h"

class APatientActor;
class UPatientBedAnimInstance;
class UBoxComponent;

/** Bed physics ownership is independent of video, quiz and belt task progress. */
UCLASS(ClassGroup = (PatientCare), meta = (BlueprintSpawnableComponent))
class HANDLINGRAGDOLLS_API UPatientBedSupportComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UPatientBedSupportComponent();
	void Initialize(APatientActor* Patient);
	void SuspendForTransfer();
	void ResumeOnBed();
	UFUNCTION(BlueprintPure, Category = "Patient|Bed")
	bool IsBedControlActive() const { return bEnabled && bInitialized && !bSuspended; }
	UFUNCTION(BlueprintPure, Category = "Patient|Bed")
	bool IsStableEdgeSeated() const { return bStableEdgeSeated; }
	void SetGrabBones(const TArray<FName>& Bones);
	void MoveSupportWithHand(FName Bone, const FVector& HandDelta);
	bool IsTorsoGrab(FName Bone) const;
	bool IsInsideMattress(const FVector& WorldLocation, float Margin = 0.0f) const;
	float GetStableTime() const { return StableTime; }
	float GetRegionRelaxation(FName Bone) const;
	float GetRegionMass(FName Bone) const;
	FVector ConstrainHandTarget(FName Bone, const FVector& Target, const FVector& CurrentContact) const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patient|Bed")
	bool bEnabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patient|Bed")
	TObjectPtr<AActor> BedActor;
	/** Bed-local mattress volume. Author to match the top and sides of the mattress. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patient|Bed")
	FVector MattressMin = FVector(12.0f, -232.0f, 72.0f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patient|Bed")
	FVector MattressMax = FVector(271.0f, -10.0f, 108.0f);
	/** Local marker on the side facing the wheelchair. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patient|Bed")
	FVector EdgeMarker = FVector(140.0f, -232.0f, 108.0f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patient|Bed")
	FVector EdgeOutward = FVector(0.0f, -1.0f, 0.0f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patient|Bed", meta = (ClampMin = "0.1"))
	float RequiredStableTime = 0.4f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patient|Bed")
	float EdgeTolerance = 25.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patient|Bed")
	float SettleSpeed = 15.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patient|Bed")
	float MuscleRecoveryTime = 0.35f;
	/** Spring acceleration, applied only over the support surface, in cm/s^2 per cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patient|Bed")
	float SupportStiffness = 35.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patient|Bed")
	float SupportDamping = 10.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patient|Bed")
	float BalanceStiffness = 45000.0f;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
private:
	UPROPERTY(Transient)
	TObjectPtr<APatientActor> Patient;
	UPROPERTY(Transient)
	TObjectPtr<UPatientBedAnimInstance> PoseDriver;
	UPROPERTY(Transient)
	TObjectPtr<UBoxComponent> MattressCollision;
	bool bInitialized = false;
	bool bSuspended = false;
	bool bStableEdgeSeated = false;
	bool bHasTorsoSupport = false;
	float StableTime = 0.0f;
	FVector PelvisAnchor = FVector::ZeroVector;
	FVector SupportedTorsoDirection = FVector::UpVector;
	TArray<FName> GrabBones;
	TArray<FTransform> TargetPose;
	TMap<FName, float> RegionRelaxation;
	TMap<FName, FQuat> RestingBodyRotations;
	FName RegionForBone(FName Bone) const;
	void UpdatePoseAndMuscles(float DeltaTime);
	void UpdateSupport(float DeltaTime);
	void UpdateSeated(float DeltaTime);
};
