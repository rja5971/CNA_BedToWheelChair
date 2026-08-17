// Fill out your copyright notice in the Description page of Project Settings.

#include "SpineMonitorComponent.h"
#include "../Interfaces/ISpineMonitorable.h"

USpineMonitorComponent::USpineMonitorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void USpineMonitorComponent::BeginPlay()
{
	Super::BeginPlay();

	// Cache the monitorable interface from our owning actor
	if (GetOwner())
	{
		MonitorTarget = Cast<IISpineMonitorable>(GetOwner());
		if (!MonitorTarget)
		{
			UE_LOG(LogTemp, Warning, TEXT("SpineMonitorComponent: Owner does not implement ISpineMonitorable!"));
			SetComponentTickEnabled(false);
		}
	}
}

void USpineMonitorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!MonitorTarget || bHasFailed) return;

	// Check for failure first
	if (MonitorTarget->IsSpineDamaged())
	{
		bHasFailed = true;
		OnSpineFailure.Broadcast();
		return;
	}

	// Get stressed bones and evaluate
	TArray<FName> StressedBones = MonitorTarget->GetStressedBones();
	float OverallStress = MonitorTarget->GetSpineStressLevel();

	bool bCurrentlyInWarning = false;

	for (const FName& BoneName : StressedBones)
	{
		float BoneStress = MonitorTarget->GetBoneStressLevel(BoneName);

		if (BoneStress >= CriticalThreshold)
		{
			OnSpineCritical.Broadcast(BoneName, BoneStress);
			bCurrentlyInWarning = true;
		}
		else if (BoneStress >= WarningThreshold)
		{
			OnSpineWarning.Broadcast(BoneName, BoneStress);
			bCurrentlyInWarning = true;
		}
	}

	// Also check overall stress
	if (OverallStress >= WarningThreshold)
	{
		bCurrentlyInWarning = true;
	}

	// Detect transitions
	if (bWasInWarningLastFrame && !bCurrentlyInWarning)
	{
		// Returned to safe state
		OnSpineSafe.Broadcast();
	}

	bIsInWarning = bCurrentlyInWarning;
	bWasInWarningLastFrame = bCurrentlyInWarning;
}

float USpineMonitorComponent::GetOverallStress() const
{
	if (!MonitorTarget) return 0.0f;
	return MonitorTarget->GetSpineStressLevel();
}

void USpineMonitorComponent::SetMonitorTarget(AActor* Target)
{
	if (Target)
	{
		MonitorTarget = Cast<IISpineMonitorable>(Target);
		if (MonitorTarget)
		{
			SetComponentTickEnabled(true);
			bHasFailed = false;
			bIsInWarning = false;
			UE_LOG(LogTemp, Log, TEXT("SpineMonitorComponent: Now monitoring %s"), *Target->GetName());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("SpineMonitorComponent: %s does not implement ISpineMonitorable!"), *Target->GetName());
		}
	}
}
