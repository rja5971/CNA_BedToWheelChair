// Fill out your copyright notice in the Description page of Project Settings.

#include "BeltActor.h"
#include "../Components/BeltComponent.h"
#include "../Components/GrabComponent.h"
#include "../Interfaces/IBeltAttachable.h"
#include "../Patient/PatientActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SplineComponent.h"
#include "Components/SplineMeshComponent.h"
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

	// Keep the cylinder as an invisible, stable pickup/physics proxy. The spline
	// meshes below are visual-only and never participate in Chaos simulation.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMesh.Succeeded())
	{
		BeltMesh->SetStaticMesh(CylinderMesh.Object);
		BeltMesh->SetRelativeScale3D(FVector(0.4f, 0.4f, 0.05f));
	}
	BeltMesh->SetVisibility(false);
	BeltMesh->SetHiddenInGame(true);

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

	// Spline component
	BeltSpline = CreateDefaultSubobject<USplineComponent>(TEXT("BeltSpline"));
	BeltSpline->SetupAttachment(RootComponent);
	// The hidden physics proxy is non-uniformly scaled; the visible belt and its
	// handle must remain in centimetres rather than inheriting that proxy scale.
	BeltSpline->SetAbsolute(false, false, true);
	BeltSpline->SetClosedLoop(true, false);
	BeltSpline->SetDefaultUpVector(FVector::UpVector, ESplineCoordinateSpace::Local);
	HandleFront->SetupAttachment(BeltSpline);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		SplineSegmentMesh = CubeMesh.Object;
	}
}

void ABeltActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	EnsureSplinePointCount();
	UpdateSplineForProgress(bWrapComplete ? 1.0f : WrapProgress);
	RebuildSplineMeshes();
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

	// Sync the proximity sphere to the configurable radius
	if (AttachProximity)
	{
		AttachProximity->SetSphereRadius(AttachRadius);
		AttachProximity->SetHiddenInGame(!bShowDetectionRadius);
		AttachProximity->SetVisibility(bShowDetectionRadius);
	}

	EnsureSplinePointCount();
	ResetWrapAnimation();
	RebuildSplineMeshes();
	SuppressLegacyBlueprintBeltVisuals();
}

void ABeltActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Animate the belt spline
	AnimateBelt(DeltaTime);

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
	}
}

void ABeltActor::RebuildSplineMeshes()
{
	if (!BeltSpline || !SplineSegmentMesh) return;

	EnsureSplinePointCount();
	RuntimeSplineMeshes.RemoveAll([](const TObjectPtr<USplineMeshComponent>& SplineMesh)
	{
		return !IsValid(SplineMesh);
	});

	int32 NumPoints = BeltSpline->GetNumberOfSplinePoints();
	if (NumPoints < 2) return;

	int32 NumSegments = BeltSpline->IsClosedLoop() ? NumPoints : NumPoints - 1;

	while (RuntimeSplineMeshes.Num() > NumSegments)
	{
		if (USplineMeshComponent* ExtraSegment = RuntimeSplineMeshes.Pop())
		{
			ExtraSegment->DestroyComponent();
		}
	}

	while (RuntimeSplineMeshes.Num() < NumSegments)
	{
		USplineMeshComponent* SplineMesh = NewObject<USplineMeshComponent>(this, USplineMeshComponent::StaticClass());
		SplineMesh->CreationMethod = EComponentCreationMethod::UserConstructionScript;
		SplineMesh->SetupAttachment(BeltSpline);
		SplineMesh->SetMobility(EComponentMobility::Movable);
		SplineMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SplineMesh->RegisterComponentWithWorld(GetWorld());
		RuntimeSplineMeshes.Add(SplineMesh);
	}

	for (USplineMeshComponent* SplineMesh : RuntimeSplineMeshes)
	{
		if (!SplineMesh) continue;
		SplineMesh->SetStaticMesh(SplineSegmentMesh);
		SplineMesh->SetForwardAxis(ESplineMeshAxis::X, false);
		SplineMesh->SetSplineUpDir(FVector::UpVector, false);
		SplineMesh->SetStartScale(SplineMeshCrossSectionScale, false);
		SplineMesh->SetEndScale(SplineMeshCrossSectionScale, false);
		if (SplineSegmentMaterial)
		{
			SplineMesh->SetMaterial(0, SplineSegmentMaterial);
		}
	}

	UpdateSplineMeshSegments();
}

void ABeltActor::AnimateBelt(float DeltaTime)
{
	if (!bWrapAnimating)
	{
		return;
	}

	WrapElapsed += DeltaTime;
	WrapProgress = WrapDuration <= KINDA_SMALL_NUMBER
		? 1.0f
		: FMath::Clamp(WrapElapsed / WrapDuration, 0.0f, 1.0f);
	UpdateSplineForProgress(WrapProgress);

	if (WrapProgress >= 1.0f)
	{
		bWrapAnimating = false;
		bWrapComplete = true;
		if (HandleFrontVisual)
		{
			HandleFrontVisual->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		}
		UE_LOG(LogTemp, Log, TEXT("BeltActor: Waist wrap animation completed."));
	}
}

void ABeltActor::StartWrapAnimation()
{
	bWrapAnimating = true;
	bWrapComplete = false;
	WrapElapsed = 0.0f;
	WrapProgress = 0.0f;
	if (HandleFrontVisual)
	{
		HandleFrontVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	UpdateSplineForProgress(0.0f);
	UE_LOG(LogTemp, Log, TEXT("BeltActor: Waist wrap animation started."));
}

void ABeltActor::ResetWrapAnimation()
{
	bWrapAnimating = false;
	bWrapComplete = false;
	WrapElapsed = 0.0f;
	WrapProgress = 0.0f;
	if (HandleFrontVisual)
	{
		HandleFrontVisual->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	}
	UpdateSplineForProgress(0.0f);
}

void ABeltActor::EnsureSplinePointCount()
{
	if (!BeltSpline) return;

	const int32 DesiredPointCount = FMath::Clamp(SplineSegmentCount, 8, 32);
	if (BeltSpline->GetNumberOfSplinePoints() == DesiredPointCount && BeltSpline->IsClosedLoop())
	{
		return;
	}

	BeltSpline->ClearSplinePoints(false);
	for (int32 PointIndex = 0; PointIndex < DesiredPointCount; ++PointIndex)
	{
		BeltSpline->AddSplinePoint(FVector::ZeroVector, ESplineCoordinateSpace::Local, false);
		BeltSpline->SetSplinePointType(PointIndex, ESplinePointType::Curve, false);
	}
	BeltSpline->SetClosedLoop(true, false);
	BeltSpline->UpdateSpline();
}

void ABeltActor::UpdateSplineForProgress(float InProgress)
{
	if (!BeltSpline) return;
	EnsureSplinePointCount();

	const int32 NumPoints = BeltSpline->GetNumberOfSplinePoints();
	if (NumPoints < 2) return;

	const float Alpha = FMath::SmoothStep(0.0f, 1.0f, FMath::Clamp(InProgress, 0.0f, 1.0f));
	for (int32 PointIndex = 0; PointIndex < NumPoints; ++PointIndex)
	{
		const float Angle = (2.0f * PI) * static_cast<float>(PointIndex) / static_cast<float>(NumPoints);
		const FVector Point = FMath::Lerp(GetLooseSplinePoint(Angle), GetFittedSplinePoint(Angle), Alpha);
		BeltSpline->SetLocationAtSplinePoint(PointIndex, Point, ESplineCoordinateSpace::Local, false);
		BeltSpline->SetSplinePointType(PointIndex, ESplinePointType::Curve, false);
	}
	BeltSpline->UpdateSpline();

	if (HandleFront)
	{
		const FVector LooseHandleLocation = GetLooseSplinePoint(0.0f);
		HandleFront->SetRelativeLocation(FMath::Lerp(LooseHandleLocation, HandleOffset, Alpha));
	}

	UpdateSplineMeshSegments();
}

void ABeltActor::UpdateSplineMeshSegments()
{
	if (!BeltSpline || RuntimeSplineMeshes.IsEmpty()) return;

	const int32 NumPoints = BeltSpline->GetNumberOfSplinePoints();
	for (int32 SegmentIndex = 0; SegmentIndex < RuntimeSplineMeshes.Num(); ++SegmentIndex)
	{
		USplineMeshComponent* SplineMesh = RuntimeSplineMeshes[SegmentIndex];
		if (!SplineMesh || NumPoints < 2) continue;

		const int32 StartPointIndex = SegmentIndex % NumPoints;
		const int32 EndPointIndex = (StartPointIndex + 1) % NumPoints;
		FVector StartPos;
		FVector StartTangent;
		FVector EndPos;
		FVector EndTangent;
		BeltSpline->GetLocationAndTangentAtSplinePoint(StartPointIndex, StartPos, StartTangent,
			ESplineCoordinateSpace::Local);
		BeltSpline->GetLocationAndTangentAtSplinePoint(EndPointIndex, EndPos, EndTangent,
			ESplineCoordinateSpace::Local);
		SplineMesh->SetStartAndEnd(StartPos, StartTangent, EndPos, EndTangent, true);
	}
}

void ABeltActor::SuppressLegacyBlueprintBeltVisuals()
{
	// BP_Belt predates the native runtime belt and owns a second construction-
	// script spline plus a button-driven reveal timeline. Keep those objects alive
	// for Blueprint reference safety, but permanently hide their generated visual
	// sections so the old ring cannot render over the wrapping belt.
	TInlineComponentArray<USplineMeshComponent*> AllSplineMeshes(this);
	for (USplineMeshComponent* SplineMesh : AllSplineMeshes)
	{
		if (!SplineMesh || RuntimeSplineMeshes.Contains(SplineMesh)) continue;
		SplineMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SplineMesh->SetVisibility(false, true);
		SplineMesh->SetHiddenInGame(true, true);
		SplineMesh->Deactivate();
	}

	// The legacy widget is only capable of replaying that obsolete reveal
	// timeline. Hide and disable it without taking a C++ dependency on UMG.
	TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(this);
	for (UPrimitiveComponent* Primitive : PrimitiveComponents)
	{
		if (!Primitive) continue;
		FString NormalizedName = Primitive->GetName();
		NormalizedName.ReplaceInline(TEXT("_"), TEXT(" "));
		if (NormalizedName.Contains(TEXT("Belt animation play widget"), ESearchCase::IgnoreCase))
		{
			Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Primitive->SetVisibility(false, true);
			Primitive->SetHiddenInGame(true, true);
			Primitive->SetComponentTickEnabled(false);
			Primitive->Deactivate();
		}
	}
}

FVector ABeltActor::GetLooseSplinePoint(float AngleRadians) const
{
	const float CosAngle = FMath::Cos(AngleRadians);
	const float SinAngle = FMath::Sin(AngleRadians);
	return FVector(
		LooseForwardOffset + LooseHalfDepth * CosAngle,
		LooseHalfWidth * SinAngle,
		-0.5f * LooseDrop * (1.0f - CosAngle));
}

FVector ABeltActor::GetFittedSplinePoint(float AngleRadians) const
{
	return FVector(
		WaistHalfDepth * FMath::Cos(AngleRadians),
		WaistHalfWidth * FMath::Sin(AngleRadians),
		WaistVerticalOffset);
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
	if (!bWrapComplete)
	{
		return false;
	}

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
		if (!bWrapComplete) return;
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
			IIBeltAttachable* BeltTarget = Cast<IIBeltAttachable>(Patient);
			if (BeltTarget)
			{
				return BeltTarget->GetBeltAttachBoneName();
			}
		}
	}
	return NAME_None;
}

bool ABeltActor::RequiresRotationConstraint() const
{
	// If the belt is attached to the patient, we are lifting a heavy human.
	// We do NOT want a rotation constraint, otherwise turning the VR wrist will
	// rigidly spin the patient in the air like a propeller.
	if (BeltComp && BeltComp->IsAttached())
	{
		return false;
	}

	// If the belt is unattached and being carried, we DO want a rotation constraint
	// so it acts like a rigid tool in the hand, making it easier to aim at the patient.
	return true;
}

bool ABeltActor::ShouldUsePhysicsHandle() const
{
	if (BeltComp && BeltComp->IsAttached())
	{
		if (const APatientActor* Patient = Cast<APatientActor>(BeltComp->GetAttachedPatient()))
		{
			return !Patient->CanUseKinematicCarry();
		}
	}
	return true;
}

FVector ABeltActor::GetHandleWorldLocation() const
{
	return HandleFront ? HandleFront->GetComponentLocation() : GetActorLocation();
}
