// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "../Patient/PatientTypes.h"
#include "../Patient/PatientBoneMapping.h"
#include "../Patient/PatientStateConfig.h"
#include "PhysicsEngine/PhysicalAnimationComponent.h"
#include "PatientPhysicsComponent.generated.h"

class USkeletalMeshComponent;
class USpineConstraintConfig;
class UAnimSequence;
class UPhysicsConstraintComponent;

/**
 * Patient Physics Component — owns the physical animation lifecycle.
 *
 * Single Responsibility: Manages physics body configuration (mass, damping),
 * physical animation mode (hybrid vs ragdoll), and profile switching.
 *
 * Extracted from PatientActor to isolate physics concerns from state/grab/belt logic.
 * This component operates on any SkeletalMeshComponent provided to it.
 */
UCLASS(ClassGroup = (PatientCare), meta = (BlueprintSpawnableComponent))
class HANDLINGRAGDOLLS_API UPatientPhysicsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPatientPhysicsComponent();

protected:
	virtual void BeginPlay() override;

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	// ============================================================
	// Initialization
	// ============================================================

	/**
	 * Initialize the component with references to the mesh and data.
	 * Must be called before any physics operations (typically in owning actor's BeginPlay).
	 */
	void Initialize(USkeletalMeshComponent* InMesh, UPhysicalAnimationComponent* InPhysAnim,
		USpineConstraintConfig* InConfig, UPatientBoneMapping* InBoneMapping);

	// ============================================================
	// Physics Mode Control
	// ============================================================

	/**
	 * Enable hybrid physical animation (conscious patient with muscle tone).
	 * Bodies below pelvis simulate, pelvis stays kinematic as anchor.
	 */
	UFUNCTION(BlueprintCallable, Category = "Patient Physics")
	void EnablePhysicalAnimation();

	/**
	 * Enable full limp ragdoll (no motor drive). For unconscious/injured patients.
	 */
	UFUNCTION(BlueprintCallable, Category = "Patient Physics")
	void EnableRagdoll();

	/**
	 * Apply a specific physical animation profile (muscle-tone strength).
	 * Reads from SpineConfig data asset; falls back to hardcoded defaults if not found.
	 */
	UFUNCTION(BlueprintCallable, Category = "Patient Physics")
	void ApplyProfile(EPhysicalAnimProfile Profile);

	/**
	 * Apply a data-driven state config — sets each bone group's behavior
	 * (Anchored / Stiff / Free) as defined in the UPatientStateConfig asset.
	 * This is the preferred, data-driven way to configure the body per state.
	 */
	UFUNCTION(BlueprintCallable, Category = "Patient Physics")
	void ApplyStateConfig(UPatientStateConfig* Config);

	/**
	 * Clear all held/pinned bone transforms. Releases anchored bones so they
	 * can freely simulate. Call this when transitioning to a state where the
	 * pelvis should be free (e.g., BeingLifted).
	 */
	UFUNCTION(BlueprintCallable, Category = "Patient Physics")
	void ClearHeldPose();

	/** Get the currently active profile */
	UFUNCTION(BlueprintCallable, Category = "Patient Physics")
	EPhysicalAnimProfile GetActiveProfile() const { return ActiveProfile; }

	/**
	 * Apply a custom blended profile directly (used by cooperation ramp).
	 * Allows external systems to drive the physical animation with arbitrary values.
	 */
	void ApplyCustomSettings(const FPhysicalAnimationData& Settings, FName BelowBone);

	/**
	 * Apply custom settings to a specific bone subtree (e.g., weaker arms).
	 */
	void ApplyCustomSettingsForBone(const FPhysicalAnimationData& Settings, FName BoneName);

	// ============================================================
	// Body Configuration
	// ============================================================

	/**
	 * Apply mass distribution: scales all body masses uniformly so total = TargetMassKg.
	 */
	UFUNCTION(BlueprintCallable, Category = "Patient Physics")
	void ApplyMassDistribution();

	/**
	 * Apply linear/angular damping to all physics bodies.
	 */
	UFUNCTION(BlueprintCallable, Category = "Patient Physics")
	void ApplyBodyDamping();

	/**
	 * Play the rest pose animation (frozen at frame 0) so physical animation
	 * has a proper target pose. Without this, motors drive toward T-pose.
	 */
	UFUNCTION(BlueprintCallable, Category = "Patient Physics")
	void ApplyRestPose();

	/**
	 * Temporarily reduce mass on upper-body bones while the patient is being
	 * grabbed. This makes the physics handle able to pull the patient up from
	 * lying without the "pulling a thread" lag.
	 */
	UFUNCTION(BlueprintCallable, Category = "Patient Physics")
	void ApplyGrabMassReduction();

	/**
	 * Restore original mass after the grab ends.
	 */
	UFUNCTION(BlueprintCallable, Category = "Patient Physics")
	void RestoreGrabMass();

	// ============================================================
	// Pivot Rotation Control
	// ============================================================

	/**
	 * Set the target yaw for all pivot-behavior bones.
	 * Call this each frame from the pivot transfer driver (e.g., BeltComponent
	 * two-hand average yaw). Only takes effect when a state config with Pivot
	 * bones is active.
	 */
	UFUNCTION(BlueprintCallable, Category = "Patient Physics")
	void SetPivotYaw(float NewYawDegrees);

	/** Get the current pivot yaw target */
	UFUNCTION(BlueprintCallable, Category = "Patient Physics")
	float GetPivotYaw() const { return PivotTargetYaw; }

	/** Whether any pivot bones are currently active */
	UFUNCTION(BlueprintCallable, Category = "Patient Physics")
	bool IsPivotActive() const { return PivotBoneTransforms.Num() > 0; }

	// ============================================================
	// Accessors
	// ============================================================

	UPhysicalAnimationComponent* GetPhysicalAnimationComponent() const { return PhysicalAnimation; }

	/** Resolve a bone role to its actual skeleton bone name */
	FName ResolveBoneName(EPatientBoneRole BoneRole) const;

	// ============================================================
	// Configuration (EditAnywhere — tunable in editor)
	// ============================================================

	/** Target total body mass in kg */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patient Physics|Mass", meta = (ClampMin = "30.0", ClampMax = "150.0"))
	float TargetBodyMassKg = 70.0f;

	/** Whether to override mass distribution */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Patient Physics|Mass")
	bool bOverrideMass = true;

	/** Linear damping for all bodies (flesh absorbs energy) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Patient Physics|Damping", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float BodyLinearDamping = 1.0f;

	/** Angular damping for all bodies (joint friction) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Patient Physics|Damping", meta = (ClampMin = "0.0", ClampMax = "20.0"))
	float BodyAngularDamping = 2.5f;

	/**
	 * Mass scale multiplier applied to upper-body bones while the patient is
	 * being grabbed. Lower values make the patient easier to pull up.
	 * 1.0 = no change, 0.3 = 30% of original mass.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patient Physics|Mass", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float GrabMassScale = 0.1f;

	/** Rest pose animation (frozen target for physical animation motors) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Patient Physics|Animation")
	TObjectPtr<UAnimSequence> RestPoseAnimation;

private:
	/** Cached references (set via Initialize) */
	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> Mesh;

	UPROPERTY()
	TObjectPtr<UPhysicalAnimationComponent> PhysicalAnimation;

	UPROPERTY()
	TObjectPtr<USpineConstraintConfig> SpineConfig;

	UPROPERTY()
	TObjectPtr<UPatientBoneMapping> BoneMapping;

	/** Currently active profile */
	EPhysicalAnimProfile ActiveProfile = EPhysicalAnimProfile::Relaxed;

	// --- Diagnostic logging (temporary) ---
	bool bDiagLogging = false;
	float DiagElapsed = 0.0f;
	int32 DiagFrame = 0;

	// --- Grab mass reduction state ---
	bool bGrabMassReduced = false;
	TMap<FName, float> OriginalMassScales;

	// --- Pose hold (freeze anchored bones at their captured world transforms) ---
	// The pelvis becomes kinematic when Anchored. Other anchored bodies remain
	// simulated and use world constraints so they render the captured physical pose.
	// PostPhysics reassertion provides a final safeguard against drift.
	bool bHoldingPose = false;
	TMap<FName, FTransform> HeldBoneTransforms;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UPhysicsConstraintComponent>> AnchorConstraints;

	// --- Pivot (position-pinned, yaw-free) ---
	// Pivot bones hold XYZ position + pitch/roll but allow yaw to be driven.
	// PivotBoneTransforms stores the initial (pre-pivot) transform for each pivot bone.
	// PivotTargetYaw is the externally-driven yaw angle (world space, degrees).
	TMap<FName, FTransform> PivotBoneTransforms;
	float PivotTargetYaw = 0.0f;
	float PivotBaseYaw = 0.0f; // The yaw at the time the pivot config was applied

	/** Find profile data in the config data asset */
	const FPhysicalAnimProfileData* FindProfileData(EPhysicalAnimProfile Profile) const;

	/** Resolve a bone group to its subtree root bone name (via BoneMapping) */
	FName ResolveGroupRoot(EPatientBoneGroup Group) const;

	/** Destroy temporary world constraints created for Anchored behaviors. */
	void ClearAnchorConstraints();
};
