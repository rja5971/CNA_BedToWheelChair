// Fill out your copyright notice in the Description page of Project Settings.

#include "SeatedTransitionComponent.h"
#include "PatientPhysicsComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "../Transfer/WheelchairActor.h"

USeatedTransitionComponent::USeatedTransitionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;

	static ConstructorHelpers::FObjectFinder<UAnimSequence> DefaultSeatedAnimation(
		TEXT("/Game/Animations/SittingIdle_1__UE.SittingIdle_1__UE"));
	if (DefaultSeatedAnimation.Succeeded())
	{
		SeatedAnimation = DefaultSeatedAnimation.Object;
	}

	static ConstructorHelpers::FClassFinder<UAnimInstance> DefaultSeatedAnimClass(
		TEXT("/Game/Animations/ABP_Patient_SeatedIK"));
	if (DefaultSeatedAnimClass.Succeeded())
	{
		SeatedAnimClass = DefaultSeatedAnimClass.Class;
	}
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
	if (!bBlending || !Mesh) return;

	BlendElapsed += DeltaTime;
	const float LinearAlpha = FMath::Clamp(BlendElapsed / FMath::Max(BlendDuration, 0.1f), 0.0f, 1.0f);
	const float TorsoAlpha = LinearAlpha * LinearAlpha * (3.0f - 2.0f * LinearAlpha);
	const float LimbLinearAlpha = FMath::Clamp(
		(LinearAlpha - LimbBlendDelay) / FMath::Max(1.0f - LimbBlendDelay, KINDA_SMALL_NUMBER), 0.0f, 1.0f);
	const float LimbAlpha = LimbLinearAlpha * LimbLinearAlpha * (3.0f - 2.0f * LimbLinearAlpha);

	if (LinearAlpha < 0.35f && GetTorsoUprightAngleDeg() > SitUprightAngleThreshold + CancelAngleGrace)
	{
		UE_LOG(LogTemp, Log, TEXT("SeatedTransition: Patient fell away before the handoff; cancelling."));
		CancelBlend();
		return;
	}

	ApplyPhysicsBlend(TorsoAlpha, LimbAlpha);
	if (LinearAlpha >= 1.0f)
	{
		CompleteBlend();
	}
}

float USeatedTransitionComponent::GetTorsoUprightAngleDeg() const
{
	if (!Mesh) return 90.0f;
	const FName PelvisBone = ResolveBoneName(EPatientBoneRole::Pelvis);
	const FName ChestBone = ResolveBoneName(EPatientBoneRole::Spine05);
	if (PelvisBone.IsNone() || ChestBone.IsNone()) return 90.0f;

	FVector TorsoDir = Mesh->GetBoneLocation(ChestBone) - Mesh->GetBoneLocation(PelvisBone);
	if (!TorsoDir.Normalize()) return 90.0f;
	const float Dot = FMath::Clamp(FVector::DotProduct(TorsoDir, FVector::UpVector), -1.0f, 1.0f);
	return FMath::RadiansToDegrees(FMath::Acos(Dot));
}

bool USeatedTransitionComponent::CheckSitThreshold(float TorsoAngle)
{
	if (bSeatedLocked || bBlending) return false;
	if (TorsoAngle <= SitUprightAngleThreshold)
	{
		UE_LOG(LogTemp, Log, TEXT("SeatedTransition: Upright threshold crossed at %.1f degrees."), TorsoAngle);
		return true;
	}
	return false;
}

void USeatedTransitionComponent::BeginSeatedSettle()
{
	UAnimSequence* BedAnim = BedSeatedAnimation ? BedSeatedAnimation : SeatedAnimation;
	if (!BedAnim || !Mesh || !PhysicsComp) return;

	bHasSeatTarget = false;
	FName PelvisBone = ResolveBoneName(EPatientBoneRole::Pelvis);

	// 1. Capture current Pelvis transform while physics is active
	FTransform CapturedPelvis = FTransform::Identity;
	if (!PelvisBone.IsNone())
	{
		CapturedPelvis = Mesh->GetSocketTransform(PelvisBone);
	}

				// 2. Stop physics completely and switch to QueryOnly to prevent depenetration explosions
	// If the new animated pose clips into the bed (e.g. legs hanging down through the mattress),
	// the physics engine will violently eject the patient out of the map if collision is still active.
	PhysicsComp->ClearHeldPose(); // MUST BE CALLED FIRST! ClearHeldPose might re-enable physics on Anchored bones.
	Mesh->SetSimulatePhysics(false);
	Mesh->SetAllBodiesSimulatePhysics(false);
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);

	// CLEAR ROTATION: Force the mesh perfectly upright before the animation takes over.
	// As requested, this clears the -90 X rotation (and any other Pitch/Roll) so the 
	// animation evaluates properly without being tilted. 
	// (Our Pelvis alignment step below will restore the correct facing Yaw).
	Mesh->SetWorldRotation(FRotator::ZeroRotator, false, nullptr, ETeleportType::TeleportPhysics);

	// 3. Play animation
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Mesh->SetAnimation(BedAnim);
	Mesh->SetPosition(0.0f);
	Mesh->SetPlayRate(1.0f);
	Mesh->Play(bLoopSeatedAnimation);

		// 4. Align Mesh so the new Anim Pelvis matches the Captured physics Pelvis
	if (!PelvisBone.IsNone())
	{
		Mesh->TickAnimation(0.0f, false);
		Mesh->RefreshBoneTransforms();

		// CORRECT ORDER: Rotate the mesh first, then update bones, then translate.
		// If we translate first and then rotate, the Pelvis swings in a huge arc away from the target!
				FVector StartLoc = Mesh->GetComponentLocation();
		
		FRotator AnimPelvisRot = Mesh->GetSocketRotation(PelvisBone);
		float YawDiff = CapturedPelvis.Rotator().Yaw - AnimPelvisRot.Yaw;
		Mesh->AddWorldRotation(FRotator(0.0f, YawDiff, 0.0f), false, nullptr, ETeleportType::TeleportPhysics);
		
		Mesh->RefreshBoneTransforms();

		FVector AnimPelvisLoc = Mesh->GetSocketLocation(PelvisBone);
		FVector LocDiff = CapturedPelvis.GetLocation() - AnimPelvisLoc;
		Mesh->AddWorldOffset(LocDiff, false, nullptr, ETeleportType::TeleportPhysics);
		
		FVector EndLoc = Mesh->GetComponentLocation();

		UE_LOG(LogTemp, Warning, TEXT("CATAPULT DEBUG: StartActorLoc=%s, EndActorLoc=%s, LocDiff=%s"),
			*StartLoc.ToString(),
			*EndLoc.ToString(),
			*LocDiff.ToString());
	}

	bBlending = false;
	bSeatedLocked = true;
	SetComponentTickEnabled(false);

	UE_LOG(LogTemp, Log, TEXT("SeatedTransition: Instantly snapped and aligned to bed seated animation."));
	OnSeatedReached.Broadcast();

	if (bDisablePhysicsAfterBedBlend && !bBedSeatedFinalized)
	{
		FinalizeBedSeatedBlend();
	}
}

void USeatedTransitionComponent::BeginSeatedBlendToTarget(const FTransform& SeatTarget, AWheelchairActor* Wheelchair)
{
	TargetWheelchair = Wheelchair;
	SnapToAnimationAtTarget(SeatTarget);
}

void USeatedTransitionComponent::MarkSeated()
{
	bSeatedLocked = true;
	bBlending = false;
	SetComponentTickEnabled(false);
}

void USeatedTransitionComponent::ResetTransition()
{
	bSeatedLocked = false;
	bBlending = false;
	bHasSeatTarget = false;
	bBedSeatedFinalized = false;
	TargetWheelchair.Reset();
	BlendElapsed = 0.0f;
	SetComponentTickEnabled(false);
	if (Mesh)
	{
		Mesh->SetAllBodiesPhysicsBlendWeight(1.0f, false);
	}
}

void USeatedTransitionComponent::StartBlend(const FTransform* SeatTarget)
{
	if (!Mesh || !PhysicsComp || bBlending || bSeatedLocked) return;
	if (!SeatedAnimation)
	{
		UE_LOG(LogTemp, Error, TEXT("SeatedTransition: No SeatedAnimation is assigned."));
		return;
	}

	USkeletalMesh* SkeletalMeshAsset = Mesh->GetSkeletalMeshAsset();
	if (!SkeletalMeshAsset || SeatedAnimation->GetSkeleton() != SkeletalMeshAsset->GetSkeleton())
	{
		UE_LOG(LogTemp, Error, TEXT("SeatedTransition: Animation '%s' does not use patient skeleton '%s'. Retarget it before seating."),
			*GetNameSafe(SeatedAnimation), *GetNameSafe(SkeletalMeshAsset ? SkeletalMeshAsset->GetSkeleton() : nullptr));
		return;
	}

	bHasSeatTarget = SeatTarget != nullptr;
	if (SeatTarget) TargetSeatTransform = *SeatTarget;

	PhysicsComp->ClearHeldPose();
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	if (bEnableSeatedFootIK && SeatedAnimClass && bHasSeatTarget)
	{
		Mesh->SetAnimInstanceClass(SeatedAnimClass);
	}
	else
	{
		Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		Mesh->SetAnimation(SeatedAnimation);
		Mesh->SetPosition(0.0f);
		Mesh->SetPlayRate(1.0f);
		Mesh->Play(bLoopSeatedAnimation);
	}

	PhysicsComp->ApplyProfile(EPhysicalAnimProfile::Seated);
	ApplyPhysicsBlend(0.0f, 0.0f);
	if (bHasSeatTarget)
	{
		// Release-driven chair seating snaps the pelvis onto the selected chair
		// immediately; the body then blends gradually from physics to animation.
		AlignAnimationToSeatTarget();
	}
	bBlending = true;
	BlendElapsed = 0.0f;
	SetComponentTickEnabled(true);

	UE_LOG(LogTemp, Log, TEXT("SeatedTransition: Blending physics to '%s' over %.2f seconds%s."),
		*SeatedAnimation->GetName(), BlendDuration, bHasSeatTarget ? TEXT(" with seat alignment") : TEXT(""));
}

void USeatedTransitionComponent::SnapToAnimationAtTarget(const FTransform& SeatTarget)
{
	if (!Mesh || !PhysicsComp || bSeatedLocked) return;
	if (!SeatedAnimation)
	{
		UE_LOG(LogTemp, Error, TEXT("SeatedTransition: No SeatedAnimation is assigned."));
		return;
	}

	USkeletalMesh* SkeletalMeshAsset = Mesh->GetSkeletalMeshAsset();
	if (!SkeletalMeshAsset || SeatedAnimation->GetSkeleton() != SkeletalMeshAsset->GetSkeleton())
	{
		UE_LOG(LogTemp, Error, TEXT("SeatedTransition: Animation '%s' does not use patient skeleton '%s'. Retarget it before seating."),
			*GetNameSafe(SeatedAnimation), *GetNameSafe(SkeletalMeshAsset ? SkeletalMeshAsset->GetSkeleton() : nullptr));
		return;
	}

	bBlending = false;
	bHasSeatTarget = true;
	TargetSeatTransform = SeatTarget;
	BlendElapsed = 0.0f;
	SetComponentTickEnabled(false);

	// The final belt release is an explicit handoff. Remove every held/pivot
	// constraint, install the seated animation, and give it complete ownership
	// in this frame instead of allowing the former ragdoll pose to blend.
	PhysicsComp->ClearHeldPose();
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	if (bEnableSeatedFootIK && SeatedAnimClass && bHasSeatTarget)
	{
		Mesh->SetAnimInstanceClass(SeatedAnimClass);
	}
	else
	{
		Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		Mesh->SetAnimation(SeatedAnimation);
		Mesh->SetPlayRate(1.0f);
		Mesh->Play(bLoopSeatedAnimation);
		Mesh->SetPosition(0.0f);
	}
	ApplyPhysicsBlend(1.0f, 1.0f);
	Mesh->SetAllBodiesSimulatePhysics(false);
	Mesh->SetSimulatePhysics(false);
	Mesh->TickAnimation(0.0f, false);
	Mesh->RefreshBoneTransforms();

	AlignAnimationToSeatTarget();
	bSeatedLocked = true;

	UE_LOG(LogTemp, Log, TEXT("SeatedTransition: Snapped directly to '%s' at the chair seat target."),
		*SeatedAnimation->GetName());
	OnSeatedReached.Broadcast();
}

void USeatedTransitionComponent::ApplyPhysicsBlend(float TorsoAlpha, float LimbAlpha)
{
	const FName Pelvis = ResolveBoneName(EPatientBoneRole::Pelvis);
	if (Pelvis.IsNone())
	{
		Mesh->SetAllBodiesPhysicsBlendWeight(1.0f - TorsoAlpha, false);
		return;
	}

	Mesh->SetAllBodiesBelowPhysicsBlendWeight(Pelvis, 1.0f - TorsoAlpha, false, true);
	static const EPatientBoneRole DelayedRoots[] = {
		EPatientBoneRole::ThighLeft, EPatientBoneRole::ThighRight,
		EPatientBoneRole::ClavicleLeft, EPatientBoneRole::ClavicleRight,
		EPatientBoneRole::Neck01
	};
	for (const EPatientBoneRole Role : DelayedRoots)
	{
		const FName Bone = ResolveBoneName(Role);
		if (!Bone.IsNone())
		{
			Mesh->SetAllBodiesBelowPhysicsBlendWeight(Bone, 1.0f - LimbAlpha, false, true);
		}
	}
}

void USeatedTransitionComponent::CompleteBlend()
{
	if (!Mesh) return;
	ApplyPhysicsBlend(1.0f, 1.0f);
	Mesh->SetAllBodiesSimulatePhysics(false);
	Mesh->SetSimulatePhysics(false);
	Mesh->RefreshBoneTransforms();
	bBlending = false;
	bSeatedLocked = true;
	SetComponentTickEnabled(false);
	UE_LOG(LogTemp, Log, TEXT("SeatedTransition: Animation now fully controls the seated patient."));
	OnSeatedReached.Broadcast();

	// Bed blend path: when settling without a seat target, broadcast so the
	// cinematic component can start its fade sequence.
	if (!bHasSeatTarget && bDisablePhysicsAfterBedBlend && !bBedSeatedFinalized)
	{
		FinalizeBedSeatedBlend();
	}
}

void USeatedTransitionComponent::FinalizeBedSeatedBlend()
{
	if (!Mesh || bBedSeatedFinalized) return;

	// Physics is already disabled by CompleteBlend. Just mark finalized and
	// notify listeners (cinematic component, etc.).
	bBedSeatedFinalized = true;
	UE_LOG(LogTemp, Log, TEXT("SeatedTransition: Bed seated blend finalized — physics fully disabled, broadcasting."));
	OnBedSeatedBlendComplete.Broadcast();
}

void USeatedTransitionComponent::AlignAnimationToSeatTarget()
{
	const FName Pelvis = ResolveBoneName(EPatientBoneRole::Pelvis);
	if (!Mesh || Pelvis.IsNone()) return;

	const FRotator CurrentRotation = Mesh->GetComponentRotation();
	const float DesiredYawDelta = FMath::FindDeltaAngleDegrees(CurrentRotation.Yaw, TargetSeatTransform.Rotator().Yaw);
	Mesh->SetWorldRotation(FRotator(CurrentRotation.Pitch, TargetSeatTransform.Rotator().Yaw, CurrentRotation.Roll),
		false, nullptr, ETeleportType::TeleportPhysics);
	Mesh->RefreshBoneTransforms();

	const FVector Correction = TargetSeatTransform.GetLocation() - Mesh->GetSocketLocation(Pelvis);
	Mesh->AddWorldOffset(Correction, false, nullptr, ETeleportType::TeleportPhysics);
	Mesh->RefreshBoneTransforms();
	UE_LOG(LogTemp, Log, TEXT("SeatedTransition: Exact seat correction %.1f cm, yaw correction %.1f degrees."),
		Correction.Size(), DesiredYawDelta);
}

void USeatedTransitionComponent::CancelBlend()
{
	bBlending = false;
	bHasSeatTarget = false;
	BlendElapsed = 0.0f;
	SetComponentTickEnabled(false);
	ApplyPhysicsBlend(0.0f, 0.0f);
	if (PhysicsComp)
	{
		PhysicsComp->ApplyRestPose();
		PhysicsComp->ApplyProfile(EPhysicalAnimProfile::Relaxed);
	}
	OnSettleCancelled.Broadcast();
}

FName USeatedTransitionComponent::ResolveBoneName(EPatientBoneRole BoneRole) const
{
	return BoneMapping ? BoneMapping->GetBoneName(BoneRole) : NAME_None;
}








