// Fill out your copyright notice in the Description page of Project Settings.

#include "BeltComponent.h"
#include "GrabComponent.h"
#include "../Interfaces/IBeltAttachable.h"
#include "../Interfaces/IPatient.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"

UBeltComponent::UBeltComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UBeltComponent::BeginPlay()
{
	Super::BeginPlay();

	// Cache the belt mesh from the owning actor
	if (GetOwner())
	{
		BeltMesh = GetOwner()->FindComponentByClass<UStaticMeshComponent>();
		if (!BeltMesh)
		{
			BeltMesh = GetOwner()->FindComponentByClass<USkeletalMeshComponent>();
		}
	}
}

bool UBeltComponent::AttachToPatient(AActor* PatientActor)
{
	if (!PatientActor || IsAttached()) return false;

	// Check if the patient supports belt attachment
	IIBeltAttachable* BeltTarget = Cast<IIBeltAttachable>(PatientActor);
	if (!BeltTarget)
	{
		return false;
	}

	// Check if the patient allows belt attachment right now
	if (!BeltTarget->CanAttachBelt())
	{
		return false;
	}

	// Get attachment info from the patient
	FTransform AttachTransform = BeltTarget->GetBeltAttachTransform();
	FName AttachBone = BeltTarget->GetBeltAttachBoneName();

	AActor* Owner = GetOwner();
	if (!Owner) return false;

	// Disable physics and collision on the belt mesh before attaching
	if (BeltMesh)
	{
		BeltMesh->SetSimulatePhysics(false);
		BeltMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	// Move the belt to the bone position (location only, zero rotation, keep original scale)
	FVector OriginalScale = Owner->GetActorScale3D();
	Owner->SetActorLocationAndRotation(AttachTransform.GetLocation(), FRotator::ZeroRotator.Quaternion());
	Owner->SetActorScale3D(OriginalScale);

	// Attach directly to the patient's skeletal mesh bone (no physics constraint needed)
	USkeletalMeshComponent* PatientMesh = PatientActor->FindComponentByClass<USkeletalMeshComponent>();
	if (PatientMesh)
	{
		Owner->AttachToComponent(PatientMesh, FAttachmentTransformRules::KeepWorldTransform, AttachBone);
	}

	// Store reference and notify
	AttachedPatient = PatientActor;
	BeltTarget->OnBeltAttached(this);
	OnBeltAttachedToPatient.Broadcast(PatientActor);

	UE_LOG(LogTemp, Log, TEXT("BeltComponent: Attached to %s at bone %s"), *PatientActor->GetName(), *AttachBone.ToString());

	return true;
}

void UBeltComponent::DetachFromPatient()
{
	if (!IsAttached()) return;

	// Notify the patient
	IIBeltAttachable* BeltTarget = Cast<IIBeltAttachable>(AttachedPatient);
	if (BeltTarget)
	{
		BeltTarget->OnBeltDetached(this);
	}

	// Detach from the patient mesh
	AActor* Owner = GetOwner();
	if (Owner)
	{
		Owner->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	}

	// Re-enable physics and collision
	if (BeltMesh)
	{
		BeltMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		BeltMesh->SetSimulatePhysics(true);
	}

	AttachedPatient = nullptr;
	OnBeltDetachedFromPatient.Broadcast();
}

// ============================================================
// Grab Delegation — called by owning BeltActor
// ============================================================

bool UBeltComponent::CanHandleBeGrabbed(FName HandleName, FVector GrabLocation) const
{
	// Belt can only be grabbed if it's attached to a patient
	if (!IsAttached()) return false;

	// Only the handles can be grabbed
	return HandleName == LeftHandleName || HandleName == RightHandleName;
}

void UBeltComponent::OnHandleGrabbed(UGrabComponent* Grabber, FName HandleName, FVector GrabLocation)
{
	if (!Grabber) return;

	ActiveGrabbers.Add(Grabber, HandleName);
	
	// Broadcast with the grabber's owning actor (safe for dynamic delegate)
	AActor* GrabberOwner = Grabber ? Grabber->GetOwner() : nullptr;
	OnBeltHandleGrabbed.Broadcast(HandleName, GrabberOwner);

	// When first handle is grabbed, set patient to BeingLifted (frees pelvis)
	if (ActiveGrabbers.Num() == 1 && AttachedPatient)
	{
		IIPatient* Patient = Cast<IIPatient>(AttachedPatient);
		if (Patient && Patient->GetPatientState() != EPatientState::BeingLifted
			&& Patient->GetPatientState() != EPatientState::BeingTransferred)
		{
			Patient->SetPatientState(EPatientState::BeingLifted);
		}
	}

	// Check for two-hand grab start
	if (ActiveGrabbers.Num() >= 2 && !bWasTwoHandGrab)
	{
		bWasTwoHandGrab = true;
		OnTwoHandGrabStarted.Broadcast();
	}
}

void UBeltComponent::OnHandleReleased(UGrabComponent* Grabber)
{
	if (!Grabber) return;
	ActiveGrabbers.Remove(Grabber);

	// Check for two-hand grab end
	if (ActiveGrabbers.Num() < 2 && bWasTwoHandGrab)
	{
		bWasTwoHandGrab = false;
		OnTwoHandGrabEnded.Broadcast();
	}
}

TArray<FName> UBeltComponent::GetHandleNames() const
{
	TArray<FName> Handles;
	Handles.Add(LeftHandleName);
	Handles.Add(RightHandleName);
	return Handles;
}


bool UBeltComponent::GetTwoHandGrabPositions(FVector& OutLeftPos, FVector& OutRightPos) const
{
	if (ActiveGrabbers.Num() < 2) return false;

	// Find the grabbers for left and right handles
	const UGrabComponent* LeftGrabber = nullptr;
	const UGrabComponent* RightGrabber = nullptr;

	for (const TPair<UGrabComponent*, FName>& Pair : ActiveGrabbers)
	{
		if (Pair.Value == LeftHandleName)
		{
			LeftGrabber = Pair.Key;
		}
		else if (Pair.Value == RightHandleName)
		{
			RightGrabber = Pair.Key;
		}
	}

	// If both hands are on the same handle, just use the first two grabbers
	if (!LeftGrabber || !RightGrabber)
	{
		auto It = ActiveGrabbers.CreateConstIterator();
		LeftGrabber = It.Key();
		++It;
		RightGrabber = It.Key();
	}

	if (LeftGrabber && LeftGrabber->GetTraceOrigin())
	{
		OutLeftPos = LeftGrabber->GetTraceOrigin()->GetComponentLocation();
	}
	if (RightGrabber && RightGrabber->GetTraceOrigin())
	{
		OutRightPos = RightGrabber->GetTraceOrigin()->GetComponentLocation();
	}

	return LeftGrabber && LeftGrabber->GetTraceOrigin()
		&& RightGrabber && RightGrabber->GetTraceOrigin();
}

float UBeltComponent::GetTwoHandYawDegrees() const
{
	FVector LeftPos, RightPos;
	if (!GetTwoHandGrabPositions(LeftPos, RightPos))
	{
		return 0.0f;
	}

	// Direction from left hand to right hand, projected onto XY plane
	FVector Direction = RightPos - LeftPos;
	Direction.Z = 0.0f;

	if (Direction.IsNearlyZero())
	{
		return 0.0f;
	}

	// The facing direction is perpendicular to the hand-to-hand line
	// (patient faces "forward" which is 90° rotated from the handle axis)
	FVector FacingDir = FVector::CrossProduct(FVector::UpVector, Direction).GetSafeNormal();

	// Convert to yaw angle
	return FMath::RadiansToDegrees(FMath::Atan2(FacingDir.Y, FacingDir.X));
}
