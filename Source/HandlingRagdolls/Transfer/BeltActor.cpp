// Fill out your copyright notice in the Description page of Project Settings.

#include "BeltActor.h"
#include "../Components/BeltComponent.h"
#include "../Components/GrabComponent.h"
#include "../Interfaces/IBeltAttachable.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"

ABeltActor::ABeltActor()
{
	PrimaryActorTick.bCanEverTick = true;

	// Belt mesh — uses a simple cylinder as placeholder if no mesh is assigned
	BeltMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BeltMesh"));
	RootComponent = BeltMesh;
	// Start frozen (no physics) so the belt sits still wherever it's placed.
	// Physics is enabled only when the nurse grabs it to carry it.
	BeltMesh->SetSimulatePhysics(false);
	BeltMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	BeltMesh->SetCollisionObjectType(ECC_WorldDynamic);
	BeltMesh->SetCollisionResponseToAllChannels(ECR_Block);
	BeltMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	BeltMesh->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Ignore);
	// Damping so the belt settles calmly instead of spinning like a coin
	BeltMesh->SetLinearDamping(1.0f);
	BeltMesh->SetAngularDamping(5.0f);

	// Load a basic cylinder as the default placeholder mesh.
	// Designers can override this in the instance details panel.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMesh.Succeeded())
	{
		BeltMesh->SetStaticMesh(CylinderMesh.Object);
		// Scale to belt-like proportions: wide & thin cylinder
		BeltMesh->SetWorldScale3D(FVector(0.4f, 0.4f, 0.05f));
	}

	// Single front handle grab point (positioned in front of the patient's waist).
	HandleFront = CreateDefaultSubobject<USceneComponent>(TEXT("BeltHandle_Front"));
	HandleFront->SetupAttachment(BeltMesh);
	HandleFront->SetRelativeLocation(FVector(40.0f, 0.0f, 0.0f));

	// Visual indicator for the handle (sphere with full collision so grab trace hits it)
	HandleFrontVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HandleFrontVisual"));
	HandleFrontVisual->SetupAttachment(HandleFront);
	HandleFrontVisual->SetRelativeLocation(FVector::ZeroVector);
	HandleFrontVisual->SetCollisionObjectType(ECC_WorldDynamic);
	HandleFrontVisual->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	HandleFrontVisual->SetCollisionResponseToAllChannels(ECR_Block);
	HandleFrontVisual->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	HandleFrontVisual->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Ignore);
	HandleFrontVisual->SetSimulatePhysics(false);
	HandleFrontVisual->SetWorldScale3D(FVector(0.18f));

	// Use sphere mesh for handle visual
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		HandleFrontVisual->SetStaticMesh(SphereMesh.Object);
	}

	// Proximity sphere — visual reference only (actual detection is distance-based in tick)
	AttachProximity = CreateDefaultSubobject<USphereComponent>(TEXT("AttachProximity"));
	AttachProximity->SetupAttachment(BeltMesh);
	AttachProximity->SetSphereRadius(30.0f);
	AttachProximity->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AttachProximity->SetHiddenInGame(true);

	// Belt logic component
	BeltComp = CreateDefaultSubobject<UBeltComponent>(TEXT("BeltComponent"));
}

void ABeltActor::BeginPlay()
{
	Super::BeginPlay();

	// Enforce collision ignores at runtime so Blueprints don't override them.
	// This prevents the belt from violently orbiting the VR hand when grabbed.
	if (BeltMesh)
	{
		BeltMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		BeltMesh->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Ignore);
	}
	if (HandleFrontVisual)
	{
		HandleFrontVisual->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		HandleFrontVisual->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Ignore);
	}

	// Apply the configurable handle offset
	if (HandleFront)
	{
		HandleFront->SetRelativeLocation(HandleOffset);
	}

	// Sync the proximity sphere to the configurable radius
	if (AttachProximity)
	{
		AttachProximity->SetSphereRadius(AttachRadius);
		AttachProximity->SetHiddenInGame(!bShowDetectionRadius);
		AttachProximity->SetVisibility(bShowDetectionRadius);
	}
}

void ABeltActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Only check for attachment while belt is being carried and not yet attached
	if (!bIsBeingCarried || !BeltComp || BeltComp->IsAttached()) return;

	// Debug visualization of detection radius
	if (bShowDetectionRadius)
	{
		DrawDebugSphere(GetWorld(), GetActorLocation(), AttachRadius,
			16, FColor::Yellow, false, -1.f, 0, 1.5f);
	}

	// Find any nearby actor that implements IBeltAttachable within AttachRadius
	FVector BeltLocation = GetActorLocation();

	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* Other = *It;
		if (Other == this) continue;

		// Must implement IBeltAttachable
		IIBeltAttachable* BeltTarget = Cast<IIBeltAttachable>(Other);
		if (!BeltTarget) continue;

		// Check distance to the belt attach point on the patient (not actor origin)
		FTransform AttachTransform = BeltTarget->GetBeltAttachTransform();
		float Distance = FVector::Dist(BeltLocation, AttachTransform.GetLocation());

		UE_LOG(LogTemp, Verbose, TEXT("BeltActor: Distance to %s attach point: %.1f (radius: %.1f)"),
			*Other->GetName(), Distance, AttachRadius);

		if (Distance > AttachRadius) continue;

		// Attempt attachment
		if (BeltComp->AttachToPatient(Other))
		{
			// Stop physics simulation — belt is now constrained to the patient
			BeltMesh->SetSimulatePhysics(false);
			bIsBeingCarried = false;

			UE_LOG(LogTemp, Log, TEXT("BeltActor: Auto-attached to %s (distance: %.1f)"), *Other->GetName(), Distance);
			return;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("BeltActor: AttachToPatient failed for %s (CanAttachBelt may be false)"), *Other->GetName());
		}
	}
}

// ============================================================
// IGrabbable Implementation — delegates to BeltComponent
// ============================================================

bool ABeltActor::CanBeGrabbed(FName BoneName, FVector GrabLocation) const
{
	if (!BeltComp) return false;

	// If belt is not attached to patient, it can always be picked up (to carry it)
	if (!BeltComp->IsAttached())
	{
		return true;
	}

	// If attached, check if the grab location is near the front handle.
	// (The handle sphere is a static mesh with no bones, so BoneName is None —
	// we validate by proximity to the handle position instead.)
	if (HandleFront)
	{
		float Dist = FVector::Dist(GrabLocation, HandleFront->GetComponentLocation());
		const float HandleGrabRadius = 40.0f;
		return Dist <= HandleGrabRadius;
	}

	return false;
}

void ABeltActor::OnGrabbed(UGrabComponent* Grabber, FName BoneName, FVector GrabLocation)
{
	if (!BeltComp || !Grabber) return;

	if (BeltComp->IsAttached())
	{
		// Single front handle
		BeltComp->OnHandleGrabbed(Grabber, FName("BeltHandle_Front"), GrabLocation);
		UE_LOG(LogTemp, Log, TEXT("BeltActor: Front handle grabbed"));
	}
	else
	{
		// Being carried — enable physics so the physics handle can move it,
		// and mark so proximity detection can trigger attachment.
		if (BeltMesh)
		{
			BeltMesh->SetSimulatePhysics(true);
			BeltMesh->WakeAllRigidBodies();
		}
		bIsBeingCarried = true;
		UE_LOG(LogTemp, Log, TEXT("BeltActor: Picked up, carrying toward patient..."));
	}
}

void ABeltActor::OnReleased(UGrabComponent* Grabber)
{
	if (!BeltComp || !Grabber) return;

	if (BeltComp->IsAttached())
	{
		BeltComp->OnHandleReleased(Grabber);
	}
	else
	{
		// Released without attaching — freeze in place so it doesn't fall/spin.
		if (BeltMesh)
		{
			BeltMesh->SetSimulatePhysics(false);
		}
		bIsBeingCarried = false;
		UE_LOG(LogTemp, Log, TEXT("BeltActor: Released without attaching (frozen in place)."));
	}
}

UPrimitiveComponent* ABeltActor::GetGrabbableComponent() const
{
	// When attached to a patient, redirect the physics grab to the patient's
	// skeletal mesh so lifting the belt actually lifts the patient.
	if (BeltComp && BeltComp->IsAttached())
	{
		AActor* Patient = BeltComp->GetAttachedPatient();
		if (Patient)
		{
			if (USkeletalMeshComponent* PatientMesh = Patient->FindComponentByClass<USkeletalMeshComponent>())
			{
				return PatientMesh;
			}
		}
	}
	return BeltMesh;
}

TArray<FName> ABeltActor::GetGrabbableBoneNames() const
{
	if (BeltComp)
	{
		return BeltComp->GetHandleNames();
	}
	return TArray<FName>();
}

FName ABeltActor::GetGrabBoneOverride() const
{
	// When attached, grab the patient's belt-attach bone (spine) so the physics
	// handle lifts the patient's torso, not the (non-simulating) belt mesh.
	if (BeltComp && BeltComp->IsAttached())
	{
		AActor* Patient = BeltComp->GetAttachedPatient();
		if (Patient)
		{
			if (IIBeltAttachable* BeltTarget = Cast<IIBeltAttachable>(Patient))
			{
				return BeltTarget->GetBeltAttachBoneName();
			}
		}
	}
	return NAME_None;
}
