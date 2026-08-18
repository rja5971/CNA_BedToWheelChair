// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "../Interfaces/IGrabbable.h"
#include "../Interfaces/IBeltAttachable.h"
#include "../Interfaces/ISpineMonitorable.h"
#include "../Interfaces/IPatient.h"
#include "PatientTypes.h"
#include "PatientBoneMapping.h"
#include "PatientActor.generated.h"

class UBeltComponent;
class USpineMonitorComponent;
class USkeletalMeshComponent;
class USpineConstraintConfig;
class UPhysicalAnimationComponent;
class UAnimSequence;
class UPatientPhysicsComponent;
class UPatientStateConfig;class USeatedTransitionComponent;
class UCooperationRampComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPatientStateChanged, EPatientState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPatientInjured);

/**
 * The Patient Actor — a physics-driven mannequin with a spine injury.
 *
 * Single Responsibility: Represents the physical patient body and its state.
 * Implements IGrabbable, IBeltAttachable, ISpineMonitorable.
 *
 * The patient CANNOT walk (no movement component). They use a HYBRID
 * PHYSICAL ANIMATION approach (UPhysicalAnimationComponent) rather than a pure
 * ragdoll: physics bodies are motor-driven toward the current animation pose so
 * the patient feels like a conscious human with muscle tone, while still yielding
 * dynamically to the nurse's hands and environmental collisions.
 */
UCLASS(BlueprintType)
class HANDLINGRAGDOLLS_API APatientActor : public AActor,
	public IIGrabbable,
	public IIBeltAttachable,
	public IISpineMonitorable,
	public IIPatient
{
	GENERATED_BODY()

public:
	APatientActor();

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
	virtual FName GetGrabBoneOverride() const override { return NAME_None; }
	virtual bool RequiresRotationConstraint() const override { return false; }

	// ============================================================
	// IBeltAttachable Implementation
	// ============================================================
	virtual bool CanAttachBelt() const override;
	virtual FTransform GetBeltAttachTransform() const override;
	virtual FName GetBeltAttachBoneName() const override;
	virtual void OnBeltAttached(UBeltComponent* Belt) override;
	virtual void OnBeltDetached(UBeltComponent* Belt) override;
	virtual bool HasBeltAttached() const override;

	// ============================================================
	// ISpineMonitorable Implementation
	// ============================================================
	virtual float GetSpineStressLevel() const override;
	virtual float GetBoneStressLevel(FName BoneName) const override;
	virtual bool IsSpineDamaged() const override;
	virtual TArray<FName> GetStressedBones() const override;
	virtual float GetSafeAngleLimit(FName BoneName) const override;
	virtual float GetCurrentAngleDeviation(FName BoneName) const override;

	// ============================================================
	// Patient State
	// ============================================================

	/** Get current patient state */
	UFUNCTION(BlueprintCallable, Category = "Patient")
	virtual EPatientState GetPatientState() const override { return CurrentState; }

	/** Set patient state (called by state machine) */
	virtual void SetPatientState(EPatientState NewState) override;

	/** Get the skeletal mesh component */
	UFUNCTION(BlueprintCallable, Category = "Patient")
	virtual USkeletalMeshComponent* GetPatientMesh() const override { return PatientMesh; }

	/**
	 * Enable hybrid physical animation on the patient.
	 * Bodies simulate physics but are motor-driven toward the animation pose.
	 * This is the default "conscious patient" mode.
	 */
	UFUNCTION(BlueprintCallable, Category = "Patient")
	void EnablePhysicalAnimation();

	/**
	 * Enable a full limp ragdoll (no motor drive).
	 * Use for unconscious patients or after injury/failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Patient")
	void EnableRagdoll();

	/**
	 * Apply a specific physical animation profile (muscle-tone strength).
	 * Looks up the profile data in the SpineConfig data asset.
	 */
	UFUNCTION(BlueprintCallable, Category = "Patient")
	void ApplyPhysicalAnimProfile(EPhysicalAnimProfile Profile);

	/** Get the currently active physical animation profile */
	UFUNCTION(BlueprintCallable, Category = "Patient")
	EPhysicalAnimProfile GetActiveProfile() const { return ActiveProfile; }

	/** Get the physical animation component (via PatientPhysics) */
	UFUNCTION(BlueprintCallable, Category = "Patient")
	UPhysicalAnimationComponent* GetPhysicalAnimationComponent() const;

	/** Get the patient physics component */
	UFUNCTION(BlueprintCallable, Category = "Patient")
	UPatientPhysicsComponent* GetPatientPhysicsComponent() const { return PatientPhysics; }

	/** Get the seated transition component */
	UFUNCTION(BlueprintCallable, Category = "Patient")
	USeatedTransitionComponent* GetSeatedTransitionComponent() const { return SeatedTransition; }

	/** Check if neck is currently being supported */
	UFUNCTION(BlueprintCallable, Category = "Patient")
	virtual bool IsNeckSupported() const override { return bNeckIsSupported; }

	/** Get world location of the patient's pelvis bone (resolved via bone mapping) */
	UFUNCTION(BlueprintCallable, Category = "Patient")
	virtual FVector GetPelvisLocation() const override;

	/** Get physics linear velocity of the patient's pelvis bone */
	UFUNCTION(BlueprintCallable, Category = "Patient")
	virtual FVector GetPelvisVelocity() const override;

	// ============================================================
	// Events
	// ============================================================

	UPROPERTY(BlueprintAssignable, Category = "Patient|Events")
	FOnPatientStateChanged OnPatientStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Patient|Events")
	FOnPatientInjured OnPatientInjured;

protected:
	// ============================================================
	// Components
	// ============================================================

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Patient")
	TObjectPtr<USkeletalMeshComponent> PatientMesh;

	/**
	 * Physical animation component — drives the physics bodies toward the
	 * animation pose so the patient behaves like a conscious human, not a
	 * limp ragdoll. This is the core of the hybrid approach.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Patient")
	TObjectPtr<UPhysicalAnimationComponent> PhysicalAnimation;

	/**
	 * Patient physics component — owns the physics lifecycle (profiles, mass,
	 * damping, rest pose). Extracted from PatientActor for SRP.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Patient")
	TObjectPtr<UPatientPhysicsComponent> PatientPhysics;

	/**
	 * Seated transition component — owns sit detection, blend-to-seated logic,
	 * and the final freeze into the seated pose. Extracted from PatientActor for SRP.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Patient")
	TObjectPtr<USeatedTransitionComponent> SeatedTransition;

	/**
	 * Cooperation ramp component — drives progressive "aliveness" during fold-up.
	 * Blends physical animation from Relaxed toward Cooperating as the patient
	 * is folded upright, simulating a conscious person engaging their muscles.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Patient")
	TObjectPtr<UCooperationRampComponent> CooperationRamp;

	// ============================================================
	// Configuration
	// ============================================================

	/** Spine constraint data asset — defines injury severity and thresholds */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Patient|Config")
	TObjectPtr<USpineConstraintConfig> SpineConfig;

	/**
	 * Per-state bone configurations. Each entry defines how the body behaves
	 * (which bone groups are Anchored / Stiff / Free) in a given state.
	 * When the patient enters a state, the matching config is applied.
	 * If no config exists for a state, falls back to the legacy profile logic.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Patient|Config")
	TArray<TObjectPtr<UPatientStateConfig>> StateConfigs;

	/**
	 * TESTING ONLY: start the patient as a fully limp ragdoll (no muscle-tone
	 * motor drive) so the nurse can freely grab and move the body ("superman"
	 * mode). Turn this OFF later to use the realistic hybrid physical animation.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patient|Config")
	bool bTestModeStartLimp = true;

	/**
	 * If true, the belt can be attached without supporting the neck first.
	 * Useful for testing the belt/transfer flow in isolation.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patient|Config")
	bool bBypassNeckSupportForBelt = false;

	/**
	 * Bone mapping data asset — resolves logical roles (Neck, Spine, Pelvis)
	 * to actual skeleton bone names. This makes the system work with ANY
	 * humanoid skeleton (UE5 Mannequin, MetaHuman, Mixamo, custom, etc.).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Patient|Config")
	TObjectPtr<UPatientBoneMapping> BoneMapping;

	/** Logical bone roles that are valid grab targets */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Patient|Config")
	TArray<EPatientBoneRole> GrabbableRoles;

	/** Logical bone role where the belt attaches */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Patient|Belt Attachment")
	EPatientBoneRole BeltAttachRole = EPatientBoneRole::Spine03;

	/**
	 * Optional: override the resolved bone name directly.
	 * If set (not "None"), this takes priority over BeltAttachRole.
	 * Use this when the BoneMapping doesn't have the exact bone you need,
	 * or to fine-tune placement on a specific skeleton.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Patient|Belt Attachment")
	FName BeltAttachBoneOverride = NAME_None;

	/** Offset from the resolved bone's transform (local space) for fine-tuning belt position */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patient|Belt Attachment")
	FVector BeltAttachOffset = FVector::ZeroVector;

	/** Rotation offset from the resolved bone's transform (local space) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patient|Belt Attachment")
	FRotator BeltAttachRotationOffset = FRotator::ZeroRotator;

	/** Logical bone roles that count as "supporting the neck" when grabbed */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Patient|Config")
	TArray<EPatientBoneRole> NeckSupportRoles;

private:
	/** Current simulation state */
	EPatientState CurrentState = EPatientState::LyingDown;

	/** Currently attached belt */
	UPROPERTY()
	TObjectPtr<UBeltComponent> AttachedBelt;

	/** Is the neck currently being supported by a grab */
	bool bNeckIsSupported = false;

	/** Current grabbers and their grab bones */
	TMap<UGrabComponent*, FName> ActiveGrabbers;

	/** Accumulated stress per bone */
	TMap<FName, float> BoneStressMap;

	/** Total accumulated damage */
	float AccumulatedDamage = 0.0f;

	/** Whether spine damage threshold has been exceeded */
	bool bSpineDamaged = false;

	/** Currently active physical animation profile */
	EPhysicalAnimProfile ActiveProfile = EPhysicalAnimProfile::Relaxed;

	/** Reference rotations for each spine bone at rest (for stress calculations) */
	TMap<FName, FQuat> RestPoseRotations;

	/** Cache rest pose rotations on begin play */
	void CacheRestPose();

	/** Update spine stress calculations */
	void UpdateSpineStress(float DeltaTime);

	/** Called when the seated settle is cancelled (patient fell back) */
	UFUNCTION()
	void OnSettleCancelled();

	/** Find the state config matching a given state; returns nullptr if none */
	UPatientStateConfig* FindStateConfig(EPatientState State) const;

	/** Check if a bone name is in the neck support group */
	bool IsNeckSupportBone(FName BoneName) const;

	/** Resolve a logical role to an actual bone name via the BoneMapping asset */
	FName ResolveBoneName(EPatientBoneRole BoneRole) const;

	/** Resolve an array of roles to bone names */
	TArray<FName> ResolveBoneNames(const TArray<EPatientBoneRole>& Roles) const;

	/** Check if a bone name maps to any of the given roles */
	bool BoneMatchesAnyRole(FName BoneName, const TArray<EPatientBoneRole>& Roles) const;
};
