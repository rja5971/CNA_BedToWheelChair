// Fill out your copyright notice in the Description page of Project Settings.

#include "SeatedTransitionComponent.h"
#include "PatientPhysicsComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "PhysicsEngine/BodyInstance.h"

USeatedTransitionComponent::USeatedTransitionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void USeatedTransitionComponent::BeginPlay()
{
	Super::BeginPlay();
}

void USeatedTransitionComponent::Initialize(USkeletalMeshComponent* InMesh, UPatientBoneMapping* InBoneMapping,
	UPatientPhysicsComponent* InPhysics)
{
	Mesh = InMesh;
	BoneMapping = InBoneMapping;
	PhysicsComp = InPhysics;
}

void USeatedTransitionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bSettling) return;

	SettleElapsed += DeltaTime;

	// SAFETY: If the torso has fallen back (patient dropped/released), cancel the settle.
	// The body isn't actually upright anymore — don't freeze it in a lying position.
	float CurrentAngle = GetTorsoUprightAngleDeg();
	if (CurrentAngle > SitUprightAngleThreshold + 15.0f) // 15° grace margin
	{
		UE_LOG(LogTemp, Log, TEXT("SeatedTransition: Torso fell back to %.1f° — cancelling settle."), CurrentAngle);
		CancelSettle();
		return;
	}

	// Check if all bodies have low angular velocity (body is settling)
	float MaxAngVel = GetMaxBodyAngularVelocity();

	if (MaxAngVel <= SettleVelocityThreshold)
	{
		StableTime += DeltaTime;
	}
	else
	{
		// Still moving — reset stable counter
		StableTime = 0.0f;
	}

	// Freeze conditions:
	// 1. Body has been stable for MinStableTime (natural settle)
	// 2. OR max time exceeded (force freeze to prevent infinite waiting)
	if (StableTime >= MinStableTime || SettleElapsed >= MaxSettleTime)
	{
		// Final check: only freeze if the torso is still reasonably upright
		if (CurrentAngle <= SitUprightAngleThreshold + 10.0f)
		{
			if (SettleElapsed >= MaxSettleTime)
			{
				UE_LOG(LogTemp, Log, TEXT("SeatedTransition: Max settle time (%.1fs) reached — freezing at %.1f°."), MaxSettleTime, CurrentAngle);
			}
			else
			{
				UE_LOG(LogTemp, Log, TEXT("SeatedTransition: Body settled naturally in %.2fs (angle: %.1f°, max ang vel: %.1f deg/s)"),
					SettleElapsed, CurrentAngle, MaxAngVel);
			}
			FreezeInPlace();
		}
		else
		{
			// Torso not upright enough even after max time — cancel
			UE_LOG(LogTemp, Log, TEXT("SeatedTransition: Body not upright (%.1f°) after max time — cancelling."), CurrentAngle);
			CancelSettle();
		}
	}
}

// ============================================================
// Sit Detection
// ============================================================

float USeatedTransitionComponent::GetTorsoUprightAngleDeg() const
{
	if (!Mesh) return 90.0f;

	FName PelvisBone = ResolveBoneName(EPatientBoneRole::Pelvis);
	FName ChestBone = ResolveBoneName(EPatientBoneRole::Spine05);
	if (PelvisBone.IsNone() || ChestBone.IsNone()) return 90.0f;

	const FVector PelvisLoc = Mesh->GetBoneLocation(PelvisBone);
	const FVector ChestLoc = Mesh->GetBoneLocation(ChestBone);

	FVector TorsoDir = ChestLoc - PelvisLoc;
	if (!TorsoDir.Normalize())
	{
		return 90.0f;
	}

	const float Dot = FMath::Clamp(FVector::DotProduct(TorsoDir, FVector::UpVector), -1.0f, 1.0f);
	return FMath::RadiansToDegrees(FMath::Acos(Dot));
}

bool USeatedTransitionComponent::CheckSitThreshold(float TorsoAngle)
{
	if (bSeatedLocked || bSettling) return false;

	// Pure detection. The actual freeze is done by the Seated state config
	// (applied via APatientActor::SetPatientState → ApplyStateConfig), which
	// holds the player-folded pose. We do NOT apply competing motors here.
	if (TorsoAngle <= SitUprightAngleThreshold)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SEAT] Threshold CROSSED: angle=%.1f <= threshold=%.1f — triggering Seated"),
			TorsoAngle, SitUprightAngleThreshold);
		return true;
	}
	// Log occasionally so we can see the angle approaching the threshold
	static float LastLoggedAngle = 999.0f;
	if (FMath::Abs(TorsoAngle - LastLoggedAngle) > 5.0f)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SEAT] Folding: angle=%.1f (threshold=%.1f)"), TorsoAngle, SitUprightAngleThreshold);
		LastLoggedAngle = TorsoAngle;
	}
	return false;
}

// ============================================================
// Seated Settle (physics-driven)
// ============================================================

void USeatedTransitionComponent::BeginSeatedSettle()
{
	if (!Mesh || !PhysicsComp) return;
	if (bSettling || bSeatedLocked) return;

	// Switch to the Seated physical animation profile — strong motors that
	// drive the body toward an upright posture. The physics system will
	// naturally settle the body from its current folded position (~45°)
	// to fully upright over the next few frames/seconds.
	PhysicsComp->ApplyProfile(EPhysicalAnimProfile::Seated);

	// Start monitoring for stabilization
	bSettling = true;
	SettleElapsed = 0.0f;
	StableTime = 0.0f;

	// Enable tick to monitor velocity
	SetComponentTickEnabled(true);

	UE_LOG(LogTemp, Log, TEXT("SeatedTransition: Seated profile applied — waiting for body to settle (threshold: %.1f deg/s, max wait: %.1fs)"),
		SettleVelocityThreshold, MaxSettleTime);
}

// ============================================================
// Freeze
// ============================================================

void USeatedTransitionComponent::FreezeInPlace()
{
	if (!Mesh) return;

	bSettling = false;

	// We DO NOT manually turn off SimulatePhysics here!
	// Doing so instantly snaps the skeletal mesh back to its rest pose (sleeping on the bed)
	// before the PatientPhysicsComponent has a chance to capture the upright transform.
	// Instead, we just broadcast the event, and ApplyStateConfig will correctly handle 
	// the physics states to lock them in place.
	bSeatedLocked = true;

	// Disable tick — done
	SetComponentTickEnabled(false);

	UE_LOG(LogTemp, Log, TEXT("SeatedTransition: Body frozen in seated position."));

	// Broadcast so the owning actor can react (state transition, etc.)
	OnSeatedReached.Broadcast();
}

void USeatedTransitionComponent::CancelSettle()
{
	bSettling = false;
	SettleElapsed = 0.0f;
	StableTime = 0.0f;

	// Disable tick
	SetComponentTickEnabled(false);

	// Revert the profile back to Relaxed — the patient isn't seated after all.
	if (PhysicsComp)
	{
		PhysicsComp->ApplyProfile(EPhysicalAnimProfile::Relaxed);
	}

	UE_LOG(LogTemp, Log, TEXT("SeatedTransition: Settle cancelled — reverting to Relaxed profile."));

	// Broadcast cancel so the owning actor can revert state
	OnSettleCancelled.Broadcast();
}

// ============================================================
// Helpers
// ============================================================

float USeatedTransitionComponent::GetMaxBodyAngularVelocity() const
{
	if (!Mesh) return 0.0f;

	float MaxAngVel = 0.0f;

	TArray<FName> AllBoneNames;
	Mesh->GetBoneNames(AllBoneNames);

	for (const FName& BoneName : AllBoneNames)
	{
		FBodyInstance* BodyInst = Mesh->GetBodyInstance(BoneName);
		if (BodyInst && BodyInst->IsInstanceSimulatingPhysics())
		{
			FVector AngVel = BodyInst->GetUnrealWorldAngularVelocityInRadians();
			float AngVelDeg = FMath::RadiansToDegrees(AngVel.Size());
			MaxAngVel = FMath::Max(MaxAngVel, AngVelDeg);
		}
	}

	return MaxAngVel;
}

FName USeatedTransitionComponent::ResolveBoneName(EPatientBoneRole BoneRole) const
{
	if (BoneMapping)
	{
		return BoneMapping->GetBoneName(BoneRole);
	}
	return NAME_None;
}
