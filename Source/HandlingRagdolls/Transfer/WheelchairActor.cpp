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
	if (SeatTarget)
	{
		return SeatTarget->GetComponentTransform();
	}
	return GetActorTransform();
}

float AWheelchairActor::GetAcceptanceRadius() const
{
	return AcceptanceRadius_Config;
}

bool AWheelchairActor::IsLocationInSeatArea(const FVector& WorldLocation, float ExtraTolerance) const
{
	if (!SeatZone) return false;
	return SeatZone->Bounds.GetBox().ExpandBy(FMath::Max(0.0f, ExtraTolerance)).IsInsideOrOn(WorldLocation);
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
