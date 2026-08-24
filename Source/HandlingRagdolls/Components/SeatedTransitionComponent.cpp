// Fill out your copyright notice in the Description page of Project Settings.

#include "SeatedTransitionComponent.h"
#include "PatientPhysicsComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/ConstructorHelpers.h"

USeatedTransitionComponent::USeatedTransitionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;

	static ConstructorHelpers::FObjectFinder<UAnimSequence> DefaultSeatedAnimation(
		TEXT("/Game/Animations/AN_Patient_Sitting.AN_Patient_Sitting"));
	if (DefaultSeatedAnimation.Succeeded())
	{
		SeatedAnimation = DefaultSeatedAnimation.Object;
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
	StartBlend(nullptr);
}

void USeatedTransitionComponent::BeginSeatedBlendToTarget(const FTransform& SeatTarget)
{
	StartBlend(&SeatTarget);
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
	Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Mesh->SetAnimation(SeatedAnimation);
	Mesh->SetPosition(0.0f);
	Mesh->SetPlayRate(1.0f);
	Mesh->Play(bLoopSeatedAnimation);

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
}

void USeatedTransitionComponent::AlignAnimationToSeatTarget()
{
	const FName Pelvis = ResolveBoneName(EPatientBoneRole::Pelvis);
	if (!Mesh || Pelvis.IsNone()) return;

	const FRotator CurrentRotation = Mesh->GetComponentRotation();
	const float DesiredYawDelta = FMath::FindDeltaAngleDegrees(CurrentRotation.Yaw, TargetSeatTransform.Rotator().Yaw);
	const float AppliedYawDelta = FMath::Clamp(DesiredYawDelta, -MaxSeatYawCorrection, MaxSeatYawCorrection);
	Mesh->SetWorldRotation(FRotator(CurrentRotation.Pitch, CurrentRotation.Yaw + AppliedYawDelta, CurrentRotation.Roll));
	Mesh->RefreshBoneTransforms();

	FVector Correction = TargetSeatTransform.GetLocation() - Mesh->GetSocketLocation(Pelvis);
	Correction = Correction.GetClampedToMaxSize(MaxSeatPositionCorrection);
	Mesh->AddWorldOffset(Correction, false, nullptr, ETeleportType::TeleportPhysics);
	Mesh->RefreshBoneTransforms();
	UE_LOG(LogTemp, Log, TEXT("SeatedTransition: Seat correction %.1f cm, yaw correction %.1f degrees."),
		Correction.Size(), AppliedYawDelta);
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
