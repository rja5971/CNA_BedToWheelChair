// Fill out your copyright notice in the Description page of Project Settings.

#include "GrabComponent.h"
#include "PhysicsEngine/PhysicsHandleComponent.h"
#include "../Interfaces/IGrabbable.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet/KismetSystemLibrary.h"

UGrabComponent::UGrabComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UGrabComponent::BeginPlay()
{
	Super::BeginPlay();

	// Create the physics handle at runtime
	PhysicsHandle = NewObject<UPhysicsHandleComponent>(GetOwner(), TEXT("GrabPhysicsHandle"));
	if (PhysicsHandle)
	{
		PhysicsHandle->RegisterComponent();
		PhysicsHandle->LinearStiffness = GrabLinearStiffness;
		PhysicsHandle->LinearDamping = GrabLinearDamping;
		PhysicsHandle->AngularStiffness = GrabAngularStiffness;
		PhysicsHandle->AngularDamping = GrabAngularDamping;
		PhysicsHandle->InterpolationSpeed = GrabInterpolationSpeed;
	}
}

void UGrabComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Keep the physics handle target updated to follow the hand
	if (IsGrabbing())
	{
		UpdateGrabTarget();
	}
}

bool UGrabComponent::TryGrabRagdoll()
{
	// Don't double-grab
	if (IsGrabbing()) return false;

	AActor* FoundActor = nullptr;
	FName FoundBone;
	FVector FoundLocation;

	if (!FindGrabTarget(FoundActor, FoundBone, FoundLocation))
	{
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("[Grab] Target: %s bone: %s"), *GetNameSafe(FoundActor), *FoundBone.ToString());

	// Verify the actor implements IGrabbable
	IIGrabbable* Grabbable = Cast<IIGrabbable>(FoundActor);
	if (!Grabbable)
	{
		return false;
	}

	// Check if this specific bone/location can be grabbed
	if (!Grabbable->CanBeGrabbed(FoundBone, FoundLocation))
	{
		UE_LOG(LogTemp, Log, TEXT("[Grab] CanBeGrabbed=FALSE for %s bone %s"), *GetNameSafe(FoundActor), *FoundBone.ToString());
		return false;
	}

	// Get the component to grab
	UPrimitiveComponent* GrabbableComp = Grabbable->GetGrabbableComponent();
	if (!GrabbableComp)
	{
		return false;
	}

	// Allow the grabbable to override which bone the physics handle targets
	// (e.g. belt redirects the grab to the patient's spine bone so lifting works).
	FName GrabBone = FoundBone;
	FName BoneOverride = Grabbable->GetGrabBoneOverride();
	if (!BoneOverride.IsNone())
	{
		GrabBone = BoneOverride;
		// Re-center the grab location on the overridden bone
		if (USkeletalMeshComponent* SkelComp = Cast<USkeletalMeshComponent>(GrabbableComp))
		{
			FoundLocation = SkelComp->GetBoneLocation(GrabBone);
		}
	}

	// Activate the physics handle
	if (PhysicsHandle)
	{
		if (Grabbable->RequiresRotationConstraint())
		{
			PhysicsHandle->GrabComponentAtLocationWithRotation(
				GrabbableComp,
				GrabBone,
				FoundLocation,
				GrabbableComp->GetComponentRotation()
			);
		}
		else
		{
			PhysicsHandle->GrabComponentAtLocation(
				GrabbableComp,
				GrabBone,
				FoundLocation
			);
		}
	}

	// Store grab state
	GrabbedActor = FoundActor;
	GrabbedBoneName = GrabBone;
	GrabLocation = FoundLocation;

	// Notify the grabbed actor
	Grabbable->OnGrabbed(this, FoundBone, FoundLocation);

	// Broadcast event
	OnGrabStarted.Broadcast(FoundActor, GrabBone, FoundLocation);

	UE_LOG(LogTemp, Log, TEXT("[Grab] SUCCESS: %s bone %s"), *GetNameSafe(FoundActor), *GrabBone.ToString());

	return true;
}

void UGrabComponent::ReleaseRagdoll()
{
	if (!IsGrabbing()) return;

	// Release physics handle
	if (PhysicsHandle)
	{
		PhysicsHandle->ReleaseComponent();
	}

	// Notify the actor being released
	IIGrabbable* Grabbable = Cast<IIGrabbable>(GrabbedActor);
	if (Grabbable)
	{
		Grabbable->OnReleased(this);
	}

	// Broadcast event
	OnGrabEnded.Broadcast(GrabbedActor);

	// Clear state
	AActor* PreviousActor = GrabbedActor;
	GrabbedActor = nullptr;
	GrabbedBoneName = NAME_None;
	GrabLocation = FVector::ZeroVector;
}

bool UGrabComponent::FindGrabTarget(AActor*& OutActor, FName& OutBoneName, FVector& OutLocation) const
{
	// Determine trace origin — use the assigned TraceOrigin component (motion controller),
	// or fall back to owner root if not set.
	FVector TraceStart = FVector::ZeroVector;
	FVector TraceDirection = FVector::ForwardVector;

	if (TraceOrigin)
	{
		TraceStart = TraceOrigin->GetComponentLocation();
		TraceDirection = TraceOrigin->GetForwardVector();
	}
	else if (GetOwner())
	{
		USceneComponent* Root = GetOwner()->GetRootComponent();
		if (Root)
		{
			TraceStart = Root->GetComponentLocation();
			TraceDirection = Root->GetForwardVector();
		}
	}

	TArray<FHitResult> HitResults;
	TArray<AActor*> ActorsToIgnore;
	ActorsToIgnore.Add(GetOwner());

	// Short sphere trace from the hand forward
	FVector TraceEnd = TraceStart + (TraceDirection * GrabRadius);

	// Use multi-sphere trace by object type — this detects both PhysicsBody (skeletal)
	// and WorldDynamic (static meshes with BlockAllDynamic profile like belt handles)
	TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
	ObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_PhysicsBody));
	ObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_WorldDynamic));

	bool bHit = UKismetSystemLibrary::SphereTraceMultiForObjects(
		GetWorld(),
		TraceStart,
		TraceEnd,
		GrabRadius,
		ObjectTypes,
		false, // bTraceComplex
		ActorsToIgnore,
		EDrawDebugTrace::None,
		HitResults,
		true // bIgnoreSelf
	);

	if (!bHit)
	{
		return false;
	}

	// Debug: log all hits
	for (const FHitResult& Hit : HitResults)
	{
		AActor* HitActor = Hit.GetActor();
		UE_LOG(LogTemp, Log, TEXT("[GrabTrace] Hit: %s | Bone: %s | Comp: %s | Dist: %.1f"),
			*GetNameSafe(HitActor), *Hit.BoneName.ToString(),
			*GetNameSafe(Hit.GetComponent()), FVector::Dist(TraceStart, Hit.ImpactPoint));
	}

	// Find the closest hit that implements IGrabbable AND CanBeGrabbed
	float ClosestDistance = FLT_MAX;
	for (const FHitResult& Hit : HitResults)
	{
		AActor* HitActor = Hit.GetActor();
		if (!HitActor) continue;

		IIGrabbable* Grabbable = Cast<IIGrabbable>(HitActor);
		if (!Grabbable) continue;

		// Check if this specific bone/location can actually be grabbed
		if (!Grabbable->CanBeGrabbed(Hit.BoneName, Hit.ImpactPoint))
		{
			UE_LOG(LogTemp, Log, TEXT("[GrabTrace] %s bone '%s' — CanBeGrabbed=FALSE, skipping"),
				*GetNameSafe(HitActor), *Hit.BoneName.ToString());
			continue;
		}

		float Distance = FVector::Dist(TraceStart, Hit.ImpactPoint);
		if (Distance < ClosestDistance)
		{
			ClosestDistance = Distance;
			OutActor = HitActor;
			OutBoneName = Hit.BoneName;
			OutLocation = Hit.ImpactPoint;
		}
	}

	if (OutActor)
	{
		UE_LOG(LogTemp, Log, TEXT("[GrabTrace] SELECTED: %s bone '%s' dist=%.1f"),
			*GetNameSafe(OutActor), *OutBoneName.ToString(), ClosestDistance);
	}

	return OutActor != nullptr;
}

void UGrabComponent::UpdateGrabTarget()
{
	if (!PhysicsHandle || !GetOwner()) return;

	// Track the hand position (TraceOrigin = motion controller)
	FVector TargetLocation = FVector::ZeroVector;
	FRotator TargetRotation = FRotator::ZeroRotator;

	if (TraceOrigin)
	{
		TargetLocation = TraceOrigin->GetComponentLocation();
		TargetRotation = TraceOrigin->GetComponentRotation();
	}
	else
	{
		USceneComponent* Root = GetOwner()->GetRootComponent();
		if (Root)
		{
			TargetLocation = Root->GetComponentLocation();
			TargetRotation = Root->GetComponentRotation();
		}
	}

	PhysicsHandle->SetTargetLocationAndRotation(TargetLocation, TargetRotation);
}
