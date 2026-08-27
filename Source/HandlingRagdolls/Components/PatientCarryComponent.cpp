#include "PatientCarryComponent.h"

#include "GrabComponent.h"
#include "PatientPhysicsComponent.h"
#include "../Patient/PatientActor.h"
#include "../Transfer/BeltActor.h"
#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"

UPatientCarryComponent::UPatientCarryComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UPatientCarryComponent::Initialize(USkeletalMeshComponent* InMesh, UPatientPhysicsComponent* InPhysics)
{
	Mesh = InMesh;
	PhysicsComp = InPhysics;
}

bool UPatientCarryComponent::BeginCarry(UGrabComponent* Grabber, ABeltActor* BeltActor)
{
	if (!Grabber || !BeltActor || !Mesh || !PhysicsComp || !CarryAnimation)
	{
		return false;
	}

	USkeletalMesh* MeshAsset = Mesh->GetSkeletalMeshAsset();
	if (!MeshAsset || CarryAnimation->GetSkeleton() != MeshAsset->GetSkeleton())
	{
		UE_LOG(LogTemp, Error, TEXT("PatientCarry: Animation '%s' does not use patient skeleton '%s'."),
			*GetNameSafe(CarryAnimation), *GetNameSafe(MeshAsset ? MeshAsset->GetSkeleton() : nullptr));
		return false;
	}

	ActiveGrabbers.AddUnique(Grabber);
	ActiveBelt = BeltActor;
	ViewerActor = Grabber->GetOwner();
	bReleasePending = false;
	ReleaseElapsed = 0.0f;

	if (bCarryActive)
	{
		return true;
	}

	PhysicsComp->ClearHeldPose();
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Mesh->SetAnimation(CarryAnimation);
	Mesh->SetPosition(0.0f);
	Mesh->SetPlayRate(1.0f);
	Mesh->Play(true);
	Mesh->SetAllBodiesPhysicsBlendWeight(0.0f, false);
	Mesh->SetAllBodiesSimulatePhysics(false);
	Mesh->SetSimulatePhysics(false);
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Mesh->TickAnimation(0.0f, false);
	Mesh->RefreshBoneTransforms();

	LastFacingYaw = Mesh->GetComponentRotation().Yaw;
	bCarryActive = true;
	SetComponentTickEnabled(true);
	UE_LOG(LogTemp, Log, TEXT("PatientCarry: Kinematic carry started with '%s'."), *CarryAnimation->GetName());
	return true;
}

void UPatientCarryComponent::EndCarry(UGrabComponent* Grabber)
{
	ActiveGrabbers.Remove(Grabber);
	if (!bCarryActive || ActiveGrabbers.Num() > 0)
	{
		return;
	}

	bCarryActive = false;
	bReleasePending = true;
	ReleaseElapsed = 0.0f;
	UE_LOG(LogTemp, Log, TEXT("PatientCarry: Final handle released; recovery grace period started."));
}

void UPatientCarryComponent::PrepareForSeating()
{
	bCarryActive = false;
	bReleasePending = false;
	ActiveGrabbers.Empty();
	ActiveBelt = nullptr;
	ViewerActor = nullptr;
	SetComponentTickEnabled(false);
}

void UPatientCarryComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bReleasePending)
	{
		ReleaseElapsed += DeltaTime;
		if (ReleaseElapsed >= ReleaseGracePeriod)
		{
			RestorePhysicsAfterRelease();
		}
		return;
	}

	if (!bCarryActive || !Mesh || !ActiveBelt)
	{
		return;
	}

	FVector HandAnchor;
	if (!GetHandAnchor(HandAnchor))
	{
		return;
	}

	FVector ViewerLocation;
	if (GetViewerLocation(ViewerLocation))
	{
		FVector Facing = ViewerLocation - Mesh->GetComponentLocation();
		Facing.Z = 0.0f;
		if (Facing.SizeSquared() >= FMath::Square(MinimumFacingDistance))
		{
			const float DesiredYaw = Facing.Rotation().Yaw + FacingYawOffset;
			LastFacingYaw = FacingTurnSpeed <= 0.0f
				? DesiredYaw
				: FMath::FixedTurn(LastFacingYaw, DesiredYaw, FacingTurnSpeed * DeltaTime);
		}
	}

	Mesh->SetWorldRotation(FRotator(UprightPitch, LastFacingYaw, UprightRoll), false, nullptr,
		ETeleportType::TeleportPhysics);

	const FVector DesiredHandleLocation = HandAnchor + HandAnchorOffset;
	const FVector Correction = DesiredHandleLocation - ActiveBelt->GetHandleWorldLocation();
	const FVector CurrentLocation = Mesh->GetComponentLocation();
	const FVector TargetLocation = CurrentLocation + Correction;
	const FVector NewLocation = FollowInterpSpeed <= 0.0f
		? TargetLocation
		: FMath::VInterpTo(CurrentLocation, TargetLocation, DeltaTime, FollowInterpSpeed);
	Mesh->SetWorldLocation(NewLocation, false, nullptr, ETeleportType::TeleportPhysics);
}

bool UPatientCarryComponent::GetHandAnchor(FVector& OutAnchor) const
{
	FVector Sum = FVector::ZeroVector;
	int32 Count = 0;
	for (UGrabComponent* Grabber : ActiveGrabbers)
	{
		if (Grabber && Grabber->GetTraceOrigin())
		{
			Sum += Grabber->GetTraceOrigin()->GetComponentLocation();
			++Count;
		}
	}
	if (Count == 0) return false;
	OutAnchor = Sum / static_cast<float>(Count);
	return true;
}

bool UPatientCarryComponent::GetViewerLocation(FVector& OutLocation) const
{
	if (!ViewerActor) return false;

	if (const APawn* Pawn = Cast<APawn>(ViewerActor))
	{
		if (AController* Controller = Pawn->GetController())
		{
			FRotator UnusedRotation;
			Controller->GetPlayerViewPoint(OutLocation, UnusedRotation);
			return true;
		}
	}

	if (const UCameraComponent* Camera = ViewerActor->FindComponentByClass<UCameraComponent>())
	{
		OutLocation = Camera->GetComponentLocation();
		return true;
	}

	OutLocation = ViewerActor->GetActorLocation();
	return true;
}

void UPatientCarryComponent::RestorePhysicsAfterRelease()
{
	bReleasePending = false;
	ActiveBelt = nullptr;
	ViewerActor = nullptr;
	SetComponentTickEnabled(false);
	if (APatientActor* Patient = Cast<APatientActor>(GetOwner()))
	{
		Patient->RestorePhysicsAfterKinematicCarry();
	}
	UE_LOG(LogTemp, Log, TEXT("PatientCarry: Release grace expired; state physics restored."));
}
