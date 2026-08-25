// Fill out your copyright notice in the Description page of Project Settings.

#include "WheelchairActor.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

AWheelchairActor::AWheelchairActor()
{
	PrimaryActorTick.bCanEverTick = false;

	// Wheelchair mesh — uses a simple cube as placeholder if no mesh is assigned
	WheelchairMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WheelchairMesh"));
	RootComponent = WheelchairMesh;
	WheelchairMesh->SetSimulatePhysics(false); // Wheelchair stays put (brakes)
	WheelchairMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	WheelchairMesh->SetCollisionProfileName(TEXT("BlockAll"));

	// Load a basic cube as the default placeholder mesh (rough wheelchair body shape).
	// Designers can override this with a real wheelchair mesh in the instance details.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		WheelchairMesh->SetStaticMesh(CubeMesh.Object);
		// Scale to roughly chair-sized proportions
		WheelchairMesh->SetWorldScale3D(FVector(0.6f, 0.6f, 0.5f));
	}

	// Seat zone (overlap volume)
	SeatZone = CreateDefaultSubobject<UBoxComponent>(TEXT("SeatZone"));
	SeatZone->SetupAttachment(RootComponent);
	SeatZone->SetBoxExtent(FVector(30.0f, 30.0f, 20.0f));
	SeatZone->SetRelativeLocation(FVector(0.0f, 0.0f, 50.0f)); // Above wheelchair base
	SeatZone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SeatZone->SetGenerateOverlapEvents(true);
	SeatZone->ShapeColor = FColor::Green;

	// Broad, oriented recognition area. Detection is geometric rather than
	// collision-response-dependent so kinematic/query-only patients work reliably.
	ApproachZone = CreateDefaultSubobject<UBoxComponent>(TEXT("ApproachZone"));
	ApproachZone->SetupAttachment(RootComponent);
	ApproachZone->SetBoxExtent(FVector(90.0f, 75.0f, 80.0f));
	ApproachZone->SetRelativeLocation(FVector(0.0f, 0.0f, 50.0f));
	ApproachZone->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ApproachZone->SetGenerateOverlapEvents(false);
	ApproachZone->ShapeColor = FColor::Yellow;

	// Seat target (exact seating position)
	SeatTarget = CreateDefaultSubobject<USceneComponent>(TEXT("SeatTarget"));
	SeatTarget->SetupAttachment(RootComponent);
	SeatTarget->SetRelativeLocation(FVector(0.0f, 0.0f, 50.0f));
}

void AWheelchairActor::BeginPlay()
{
	Super::BeginPlay();

	if (bStartWithBrakesLocked)
	{
		LockBrakes();
	}

	if (SeatZone)
	{
		SeatZone->SetBoxExtent(SeatCommitZoneExtent.GetAbs());
		SeatZone->SetHiddenInGame(!bShowDetectionZones);
		SeatZone->SetVisibility(bShowDetectionZones);
	}
	if (ApproachZone)
	{
		ApproachZone->SetBoxExtent(ApproachZoneExtent.GetAbs());
		ApproachZone->SetHiddenInGame(!bShowDetectionZones);
		ApproachZone->SetVisibility(bShowDetectionZones);
	}
}

// ============================================================
// ITransferTarget Implementation
// ============================================================

bool AWheelchairActor::IsReadyToReceive() const
{
	// Wheelchair must have brakes locked and not already occupied
	return bBrakesLocked && !bIsOccupied;
}

FTransform AWheelchairActor::GetTargetSeatTransform() const
{
	FTransform Result = GetActorTransform();
	if (SeatTarget)
	{
		Result = SeatTarget->GetComponentTransform();
	}

	// SeatTarget controls the exact pelvis position. Chair facing is authoritative
	// for orientation, with a 180-degree calibration for the imported patient's
	// opposite skeletal forward axis.
	const FRotator ChairFacing(0.0f, GetActorRotation().Yaw - 180.0f, 0.0f);
	Result.SetRotation(ChairFacing.Quaternion());
	return Result;
}

float AWheelchairActor::GetAcceptanceRadius() const
{
	return AcceptanceRadius_Config;
}

bool AWheelchairActor::IsLocationInSeatArea(const FVector& WorldLocation, float ExtraTolerance) const
{
	if (!SeatZone) return false;
	const FVector Local = SeatZone->GetComponentTransform().InverseTransformPosition(WorldLocation);
	const FVector Extent = SeatZone->GetUnscaledBoxExtent()
		+ FVector(FMath::Max(0.0f, ExtraTolerance));
	return FMath::Abs(Local.X) <= Extent.X
		&& FMath::Abs(Local.Y) <= Extent.Y
		&& FMath::Abs(Local.Z) <= Extent.Z;
}

bool AWheelchairActor::IsLocationInApproachArea(const FVector& WorldLocation, float ExtraTolerance) const
{
	if (!ApproachZone) return false;
	const FVector Local = ApproachZone->GetComponentTransform().InverseTransformPosition(WorldLocation);
	const FVector Extent = ApproachZone->GetUnscaledBoxExtent()
		+ FVector(FMath::Max(0.0f, ExtraTolerance));
	return FMath::Abs(Local.X) <= Extent.X
		&& FMath::Abs(Local.Y) <= Extent.Y
		&& FMath::Abs(Local.Z) <= Extent.Z;
}

float AWheelchairActor::GetSeatDistanceSquared(const FVector& WorldLocation) const
{
	return FVector::DistSquared(WorldLocation, GetTargetSeatTransform().GetLocation());
}

void AWheelchairActor::OnTransferBegin(AActor* Patient)
{
	// Could play wheelchair creak sound, lock wheels tighter, etc.
	UE_LOG(LogTemp, Log, TEXT("Wheelchair: Transfer beginning for %s"), *GetNameSafe(Patient));
}

void AWheelchairActor::OnTransferComplete(AActor* Patient)
{
	bIsOccupied = true;
	OnWheelchairTransferComplete.Broadcast();
	UE_LOG(LogTemp, Log, TEXT("Wheelchair: Patient %s successfully seated"), *GetNameSafe(Patient));
}

void AWheelchairActor::OnTransferFailed(AActor* Patient)
{
	UE_LOG(LogTemp, Warning, TEXT("Wheelchair: Transfer failed for %s"), *GetNameSafe(Patient));
}

// ============================================================
// Wheelchair Operations
// ============================================================

void AWheelchairActor::LockBrakes()
{
	bBrakesLocked = true;
	// Ensure the wheelchair cannot be moved
	if (WheelchairMesh)
	{
		WheelchairMesh->SetSimulatePhysics(false);
	}
}

void AWheelchairActor::UnlockBrakes()
{
	bBrakesLocked = false;
}
