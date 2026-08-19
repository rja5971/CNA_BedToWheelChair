// Fill out your copyright notice in the Description page of Project Settings.

#include "PatientActor.h"
#include "../Components/GrabComponent.h"
#include "../Components/BeltComponent.h"
#include "../Components/PatientPhysicsComponent.h"
#include "../Components/SeatedTransitionComponent.h"
#include "../Components/CooperationRampComponent.h"
#include "PatientStateConfig.h"
#include "Components/SkeletalMeshComponent.h"
#include "PhysicsEngine/PhysicalAnimationComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Animation/AnimSequence.h"
#include "Engine/Engine.h"

APatientActor::APatientActor()
{
	PrimaryActorTick.bCanEverTick = true;

	// Create the skeletal mesh component — this IS the patient's body
	PatientMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("PatientMesh"));
	RootComponent = PatientMesh;

	// Physics setup — patient is physics-driven, cannot move on their own.
	PatientMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	PatientMesh->SetCollisionProfileName(TEXT("PhysicsActor"));
	PatientMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);

	// Create the physical animation component (raw engine component)
	PhysicalAnimation = CreateDefaultSubobject<UPhysicalAnimationComponent>(TEXT("PhysicalAnimation"));

	// Create the patient physics component (our SRP wrapper that owns the lifecycle)
	PatientPhysics = CreateDefaultSubobject<UPatientPhysicsComponent>(TEXT("PatientPhysics"));

	// Create the seated transition component (owns sit detection and blend-to-seated)
	SeatedTransition = CreateDefaultSubobject<USeatedTransitionComponent>(TEXT("SeatedTransition"));

	// Create the cooperation ramp component (owns progressive aliveness during fold-up)
	CooperationRamp = CreateDefaultSubobject<UCooperationRampComponent>(TEXT("CooperationRamp"));

	// Default grabbable roles (resolved to bone names at runtime via BoneMapping).
	// NOTE: These must correspond to bones that have PHYSICS BODIES in the physics
	// asset — the grab trace hits physics bodies, not arbitrary bones. The default
	// UE mannequin physics asset covers the neck region with the "head" body.
	GrabbableRoles = {
		EPatientBoneRole::Head,
		EPatientBoneRole::Neck01,
		EPatientBoneRole::Neck02,
		EPatientBoneRole::Spine05,
		EPatientBoneRole::Spine04,
		EPatientBoneRole::Spine03,
		EPatientBoneRole::ClavicleLeft,
		EPatientBoneRole::ClavicleRight
	};

	// Roles that count as neck support (grabbing these = supporting the head/neck)
	NeckSupportRoles = {
		EPatientBoneRole::Head,
		EPatientBoneRole::Neck01,
		EPatientBoneRole::Neck02
	};
}

void APatientActor::BeginPlay()
{
	Super::BeginPlay();

	// Enforce collision ignores at runtime so Blueprints don't override them.
	// This prevents the VR player from pushing the patient out of position.
	if (PatientMesh)
	{
		PatientMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	}

	// Initialize the physics component with our mesh and config references
	if (PatientPhysics)
	{
		PatientPhysics->Initialize(PatientMesh, PhysicalAnimation, SpineConfig, BoneMapping);
		// Apply body realism setup BEFORE enabling physics
		PatientPhysics->ApplyRestPose();
		PatientPhysics->ApplyMassDistribution();
		PatientPhysics->ApplyBodyDamping();
	}

	// Cache the rest pose for spine stress calculations
	CacheRestPose();

	// Initialize the seated transition component
	if (SeatedTransition)
	{
		SeatedTransition->Initialize(PatientMesh, BoneMapping, PatientPhysics);

		// When settle is cancelled (patient fell back), revert to BeingSupported state
		SeatedTransition->OnSettleCancelled.AddDynamic(this, &APatientActor::OnSettleCancelled);
	}

	// Initialize the cooperation ramp component
	if (CooperationRamp)
	{
		CooperationRamp->Initialize(PatientPhysics, BoneMapping);
	}

	if (bTestModeStartLimp)
	{
		// TESTING: fully limp ragdoll so the body can be freely grabbed/moved.
		EnableRagdoll();
	}
	else
	{
		// Enable physics, then apply the initial state's config if one exists.
		EnablePhysicalAnimation();

		UPatientStateConfig* InitialConfig = FindStateConfig(CurrentState);
		if (InitialConfig && PatientPhysics)
		{
			PatientPhysics->ApplyStateConfig(InitialConfig);
		}
		else
		{
			// No config for the initial state — legacy relaxed profile.
			ApplyPhysicalAnimProfile(EPhysicalAnimProfile::Relaxed);
		}
	}
}

void APatientActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Continuously monitor spine stress
	UpdateSpineStress(DeltaTime);

	// If the seated transition component is currently settling, don't run sit detection.
	if (SeatedTransition && SeatedTransition->IsSettling())
	{
		return;
	}

	// Sit detection: if the player has folded the patient upright past the threshold
	// (and we have a seated pose to lock into), snap to the seated pose and freeze so
	// the next step (belt) can proceed. Only while being handled and not already locked.
	if (SeatedTransition && !SeatedTransition->IsSeatedLocked() &&
		(CurrentState == EPatientState::LyingDown ||
		 CurrentState == EPatientState::BeingSupported ||
		 CurrentState == EPatientState::BeingLifted))
	{
		const float TorsoAngle = SeatedTransition->GetTorsoUprightAngleDeg();

		// --- ALIVENESS: Progressive cooperation during fold-up ---
		// As the nurse folds the patient upright, the patient subtly engages their
		// muscles (like a real conscious person being helped to sit). This creates
		// the "aliveness" feel — they're not a dead weight the whole way up.
		// Delegated to the CooperationRampComponent for configurability.
		if (CooperationRamp && PatientPhysics && PatientMesh && CurrentState == EPatientState::BeingSupported)
		{
			CooperationRamp->TickRamp(TorsoAngle, SeatedTransition->SitUprightAngleThreshold);
		}

		if (SeatedTransition->CheckSitThreshold(TorsoAngle))
		{
			SetPatientState(EPatientState::Seated);
		}
	}
}

// ============================================================
// IGrabbable Implementation
// ============================================================

bool APatientActor::CanBeGrabbed(FName BoneName, FVector GrabLocation) const
{
	// Cannot grab an injured patient (simulation already failed)
	if (bSpineDamaged)
	{
		return false;
	}

	// When belt is attached, disable direct patient grabbing so the nurse
	// is forced to use the belt handles. Only neck support bones remain grabbable.
	if (AttachedBelt)
	{
		return IsNeckSupportBone(BoneName);
	}

	// PHASE: allow grabbing ANY body part that has a physics body (hands, legs,
	// arms, spine, neck, etc.) so the player can articulate individual limbs.
	// The pelvis is anchored, so this can't be abused to carry the whole body.
	if (PatientMesh && PatientMesh->GetBodyInstance(BoneName) != nullptr)
	{
		return true;
	}

	// Fallback: check if this bone maps to any of the allowed grabbable roles.
	return BoneMatchesAnyRole(BoneName, GrabbableRoles);
}

void APatientActor::OnGrabbed(UGrabComponent* Grabber, FName BoneName, FVector GrabLocation)
{
	if (!Grabber) return;

	ActiveGrabbers.Add(Grabber, BoneName);

	// Check if this grab provides neck support
	if (IsNeckSupportBone(BoneName))
	{
		bNeckIsSupported = true;

		// Transition state if we were just lying down
		if (CurrentState == EPatientState::LyingDown)
		{
			SetPatientState(EPatientState::BeingSupported);
		}
	}
}

void APatientActor::OnReleased(UGrabComponent* Grabber)
{
	if (!Grabber) return;

	ActiveGrabbers.Remove(Grabber);

	// Re-evaluate neck support
	bNeckIsSupported = false;
	for (const auto& Pair : ActiveGrabbers)
	{
		if (IsNeckSupportBone(Pair.Value))
		{
			bNeckIsSupported = true;
			break;
		}
	}
}

UPrimitiveComponent* APatientActor::GetGrabbableComponent() const
{
	return PatientMesh;
}

TArray<FName> APatientActor::GetGrabbableBoneNames() const
{
	return ResolveBoneNames(GrabbableRoles);
}

// ============================================================
// IBeltAttachable Implementation
// ============================================================

bool APatientActor::CanAttachBelt() const
{
	// Belt can only be attached if:
	// 1. Patient is being supported (neck is held), or has already been seated
	//    and stabilized. Once seated, requiring a hand to remain on the neck
	//    prevents the intended one-person workflow from reaching the belt step.
	// 2. No belt is already attached
	// 3. Patient isn't already injured
	const bool bPatientIsSeated = CurrentState == EPatientState::Seated;
	const bool bNeckOk = bNeckIsSupported || bPatientIsSeated || bBypassNeckSupportForBelt;
	return bNeckOk && !AttachedBelt && !bSpineDamaged;
}

FTransform APatientActor::GetBeltAttachTransform() const
{
	if (PatientMesh)
	{
		FName BoneName = GetBeltAttachBoneName();
		if (!BoneName.IsNone())
		{
			int32 BoneIdx = PatientMesh->GetBoneIndex(BoneName);
			if (BoneIdx != INDEX_NONE)
			{
				FTransform BoneTransform = PatientMesh->GetBoneTransform(BoneIdx);
				// Apply the local-space offset for fine-tuning belt position
				FTransform OffsetTransform(BeltAttachRotationOffset.Quaternion(), BeltAttachOffset);
				return OffsetTransform * BoneTransform;
			}
		}
	}
	return FTransform::Identity;
}

FName APatientActor::GetBeltAttachBoneName() const
{
	// Direct override takes priority over role-based resolution
	if (!BeltAttachBoneOverride.IsNone())
	{
		// Validate that the override bone actually exists in the skeleton
		if (PatientMesh)
		{
			int32 BoneIdx = PatientMesh->GetBoneIndex(BeltAttachBoneOverride);
			if (BoneIdx == INDEX_NONE)
			{
				UE_LOG(LogTemp, Warning, TEXT("PatientActor: BeltAttachBoneOverride '%s' does not exist in skeleton! Falling back to role-based resolution."),
					*BeltAttachBoneOverride.ToString());
				
				// Show on-screen warning in editor/PIE
				if (GEngine)
				{
					GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Red,
						FString::Printf(TEXT("BELT ERROR: Bone '%s' not found in skeleton!"), *BeltAttachBoneOverride.ToString()));
				}
			}
			else
			{
				return BeltAttachBoneOverride;
			}
		}
		else
		{
			return BeltAttachBoneOverride;
		}
	}

	FName ResolvedName = ResolveBoneName(BeltAttachRole);

	// Validate the resolved bone name
	if (PatientMesh && !ResolvedName.IsNone())
	{
		int32 BoneIdx = PatientMesh->GetBoneIndex(ResolvedName);
		if (BoneIdx == INDEX_NONE)
		{
			UE_LOG(LogTemp, Warning, TEXT("PatientActor: Resolved belt bone '%s' (from role %d) does not exist in skeleton!"),
				*ResolvedName.ToString(), (int32)BeltAttachRole);

			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Red,
					FString::Printf(TEXT("BELT ERROR: Resolved bone '%s' not found in skeleton!"), *ResolvedName.ToString()));
			}
		}
	}

	return ResolvedName;
}

void APatientActor::OnBeltAttached(UBeltComponent* Belt)
{
	AttachedBelt = Belt;
	SetPatientState(EPatientState::BeltAttached);
}

void APatientActor::OnBeltDetached(UBeltComponent* Belt)
{
	if (AttachedBelt == Belt)
	{
		AttachedBelt = nullptr;
	}
}

bool APatientActor::HasBeltAttached() const
{
	return AttachedBelt != nullptr;
}

// ============================================================
// ISpineMonitorable Implementation
// ============================================================

float APatientActor::GetSpineStressLevel() const
{
	if (!SpineConfig || SpineConfig->DamageThreshold <= 0.0f)
	{
		return 0.0f;
	}
	return FMath::Clamp(AccumulatedDamage / SpineConfig->DamageThreshold, 0.0f, 1.0f);
}

float APatientActor::GetBoneStressLevel(FName BoneName) const
{
	const float* Stress = BoneStressMap.Find(BoneName);
	return Stress ? FMath::Clamp(*Stress, 0.0f, 1.0f) : 0.0f;
}

bool APatientActor::IsSpineDamaged() const
{
	return bSpineDamaged;
}

TArray<FName> APatientActor::GetStressedBones() const
{
	TArray<FName> Result;

	if (!SpineConfig) return Result;

	for (const FSpineBoneConfig& BoneConfig : SpineConfig->SpineBones)
	{
		const float* Stress = BoneStressMap.Find(BoneConfig.BoneName);
		if (Stress && *Stress > BoneConfig.WarningThreshold)
		{
			Result.Add(BoneConfig.BoneName);
		}
	}
	return Result;
}

float APatientActor::GetSafeAngleLimit(FName BoneName) const
{
	if (!SpineConfig) return 90.0f;

	for (const FSpineBoneConfig& BoneConfig : SpineConfig->SpineBones)
	{
		if (BoneConfig.BoneName == BoneName)
		{
			return BoneConfig.SafeSwingAngle;
		}
	}
	return 90.0f;
}

float APatientActor::GetCurrentAngleDeviation(FName BoneName) const
{
	if (!PatientMesh) return 0.0f;

	const FQuat* RestRotation = RestPoseRotations.Find(BoneName);
	if (!RestRotation) return 0.0f;

	int32 BoneIndex = PatientMesh->GetBoneIndex(BoneName);
	if (BoneIndex == INDEX_NONE) return 0.0f;

	FQuat CurrentRotation = PatientMesh->GetBoneQuaternion(BoneName, EBoneSpaces::ComponentSpace);
	FQuat DeltaRotation = RestRotation->Inverse() * CurrentRotation;

	// Return the angle of deviation in degrees
	float AngleDeg;
	FVector Axis;
	DeltaRotation.ToAxisAndAngle(Axis, AngleDeg);
	return FMath::RadiansToDegrees(AngleDeg);
}

// ============================================================
// Patient State
// ============================================================

void APatientActor::SetPatientState(EPatientState NewState)
{
	if (CurrentState != NewState)
	{
		CurrentState = NewState;
		OnPatientStateChanged.Broadcast(NewState);

		// --- DATA-DRIVEN PATH ---
		// If a state config exists for this state, apply it (Anchored/Stiff/Free
		// per bone group). This is the preferred, designer-editable path.
		UPatientStateConfig* StateConfig = FindStateConfig(NewState);
		if (StateConfig && PatientPhysics)
		{
			PatientPhysics->ApplyStateConfig(StateConfig);

			// A couple of states still need behavioral hooks beyond bone setup:
			if (NewState == EPatientState::Seated && SeatedTransition)
			{
				// The config already froze the spine in place (Anchored-hold).
				// Just mark seated so sit-detection stops re-triggering.
				SeatedTransition->MarkSeated();
			}
			else if (NewState == EPatientState::Injured)
			{
				OnPatientInjured.Broadcast();
			}
			return;
		}

		// --- LEGACY FALLBACK PATH ---
		// No config for this state — use the hardcoded profile logic.
		switch (NewState)
		{
		case EPatientState::LyingDown:
			ApplyPhysicalAnimProfile(EPhysicalAnimProfile::Relaxed);
			break;
		case EPatientState::BeingSupported:
		case EPatientState::BeltAttached:
			ApplyPhysicalAnimProfile(EPhysicalAnimProfile::Relaxed);
			break;
		case EPatientState::BeingLifted:
		case EPatientState::BeingTransferred:
		{
			// Free the pelvis so the patient can be moved via the belt.
			// Use NO motor drive (ragdoll) — otherwise the physical animation
			// would yank the body toward the frozen lying-down animation pose,
			// causing a violent snap. The patient hangs naturally from the
			// grabbed spine point; damping keeps limbs from flailing.
			if (PatientPhysics)
			{
				PatientPhysics->ClearHeldPose();
				if (PatientMesh)
				{
					PatientMesh->SetAllBodiesSimulatePhysics(true);
					PatientMesh->WakeAllRigidBodies();
				}
				// Limp profile = zero motor drive → no snap toward anim pose
				PatientPhysics->ApplyProfile(EPhysicalAnimProfile::Limp);
			}
			break;
		}
		case EPatientState::Seated:
			ApplyPhysicalAnimProfile(EPhysicalAnimProfile::Seated);
			if (SeatedTransition)
			{
				SeatedTransition->BeginSeatedSettle();
			}
			break;
		case EPatientState::Injured:
			EnableRagdoll();
			OnPatientInjured.Broadcast();
			break;
		default:
			break;
		}
	}
}

UPatientStateConfig* APatientActor::FindStateConfig(EPatientState State) const
{
	for (const TObjectPtr<UPatientStateConfig>& Config : StateConfigs)
	{
		if (Config && Config->State == State)
		{
			return Config;
		}
	}
	return nullptr;
}

void APatientActor::EnablePhysicalAnimation()
{
	if (PatientPhysics)
	{
		PatientPhysics->EnablePhysicalAnimation();
	}
}

void APatientActor::EnableRagdoll()
{
	if (PatientPhysics)
	{
		PatientPhysics->EnableRagdoll();
		ActiveProfile = EPhysicalAnimProfile::Limp;
	}
}

void APatientActor::ApplyPhysicalAnimProfile(EPhysicalAnimProfile Profile)
{
	ActiveProfile = Profile;
	if (PatientPhysics)
	{
		PatientPhysics->ApplyProfile(Profile);
	}
}

UPhysicalAnimationComponent* APatientActor::GetPhysicalAnimationComponent() const
{
	return PatientPhysics ? PatientPhysics->GetPhysicalAnimationComponent() : nullptr;
}

// ============================================================
// Internal Helpers
// ============================================================

void APatientActor::CacheRestPose()
{
	if (!PatientMesh || !SpineConfig) return;

	for (const FSpineBoneConfig& BoneConfig : SpineConfig->SpineBones)
	{
		int32 BoneIndex = PatientMesh->GetBoneIndex(BoneConfig.BoneName);
		if (BoneIndex != INDEX_NONE)
		{
			FQuat RestRot = PatientMesh->GetBoneQuaternion(BoneConfig.BoneName, EBoneSpaces::ComponentSpace);
			RestPoseRotations.Add(BoneConfig.BoneName, RestRot);
			BoneStressMap.Add(BoneConfig.BoneName, 0.0f);
		}
	}
}

void APatientActor::UpdateSpineStress(float DeltaTime)
{
	if (!SpineConfig || bSpineDamaged) return;

	for (const FSpineBoneConfig& BoneConfig : SpineConfig->SpineBones)
	{
		float Deviation = GetCurrentAngleDeviation(BoneConfig.BoneName);
		float SafeAngle = BoneConfig.SafeSwingAngle;

		float* CurrentStress = BoneStressMap.Find(BoneConfig.BoneName);
		if (!CurrentStress) continue;

		if (Deviation > SafeAngle)
		{
			// Stress accumulates when exceeding safe angle
			float ExcessRatio = (Deviation - SafeAngle) / SafeAngle;
			*CurrentStress += ExcessRatio * BoneConfig.DamageMultiplier * SpineConfig->StressAccumulationRate * DeltaTime;
			AccumulatedDamage += ExcessRatio * BoneConfig.DamageMultiplier * SpineConfig->StressAccumulationRate * DeltaTime;
		}
		else
		{
			// Stress decays when within safe limits
			*CurrentStress = FMath::Max(0.0f, *CurrentStress - SpineConfig->StressDecayRate * DeltaTime);
		}
	}

	// Check for failure
	if (AccumulatedDamage >= SpineConfig->DamageThreshold)
	{
		bSpineDamaged = true;
		SetPatientState(EPatientState::Injured);
	}
}

bool APatientActor::IsNeckSupportBone(FName BoneName) const
{
	return BoneMatchesAnyRole(BoneName, NeckSupportRoles);
}


// ============================================================
// Settle Cancelled Handler
// ============================================================

void APatientActor::OnSettleCancelled()
{
	// Patient fell back before settling — revert to previous state.
	// The physics profile is already reverted by the component.
	if (bNeckIsSupported)
	{
		CurrentState = EPatientState::BeingSupported;
	}
	else
	{
		CurrentState = EPatientState::LyingDown;
	}
	OnPatientStateChanged.Broadcast(CurrentState);

	UE_LOG(LogTemp, Log, TEXT("PatientActor: Settle cancelled — reverted to %s"),
		CurrentState == EPatientState::BeingSupported ? TEXT("BeingSupported") : TEXT("LyingDown"));
}

// ============================================================
// Bone Mapping Resolution Helpers
// ============================================================

FVector APatientActor::GetPelvisLocation() const
{
	if (!PatientMesh) return FVector::ZeroVector;

	FName PelvisBone = ResolveBoneName(EPatientBoneRole::Pelvis);
	if (PelvisBone.IsNone()) return GetActorLocation();

	return PatientMesh->GetBoneLocation(PelvisBone);
}

FVector APatientActor::GetPelvisVelocity() const
{
	if (!PatientMesh) return FVector::ZeroVector;

	FName PelvisBone = ResolveBoneName(EPatientBoneRole::Pelvis);
	if (PelvisBone.IsNone()) return FVector::ZeroVector;

	return PatientMesh->GetPhysicsLinearVelocity(PelvisBone);
}

FName APatientActor::ResolveBoneName(EPatientBoneRole BoneRole) const
{
	if (BoneMapping)
	{
		return BoneMapping->GetBoneName(BoneRole);
	}

	// No mapping assigned — log a warning and return None.
	UE_LOG(LogTemp, Warning, TEXT("PatientActor: No BoneMapping assigned! Cannot resolve bone role."));
	return NAME_None;
}

TArray<FName> APatientActor::ResolveBoneNames(const TArray<EPatientBoneRole>& Roles) const
{
	if (BoneMapping)
	{
		return BoneMapping->GetBoneNames(Roles);
	}

	UE_LOG(LogTemp, Warning, TEXT("PatientActor: No BoneMapping assigned! Cannot resolve bone roles."));
	return TArray<FName>();
}

bool APatientActor::BoneMatchesAnyRole(FName BoneName, const TArray<EPatientBoneRole>& Roles) const
{
	if (!BoneMapping) return false;

	EPatientBoneRole FoundRole;
	if (BoneMapping->GetRoleForBone(BoneName, FoundRole))
	{
		return Roles.Contains(FoundRole);
	}
	return false;
}

// End of PatientActor implementation