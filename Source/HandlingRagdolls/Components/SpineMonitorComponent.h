// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SpineMonitorComponent.generated.h"

class IISpineMonitorable;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSpineWarning, FName, BoneName, float, StressLevel);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSpineCritical, FName, BoneName, float, StressLevel);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSpineFailure);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSpineSafe);

/**
 * Spine Monitor Component — monitors spine health and emits events.
 * 
 * Single Responsibility: Only observes and reports spine state.
 * Dependency Inversion: Reads from ISpineMonitorable interface, not PatientActor directly.
 * 
 * Attach this to the patient actor. It polls the ISpineMonitorable interface every tick
 * and broadcasts events when thresholds are crossed.
 */
UCLASS(ClassGroup = (PatientCare), meta = (BlueprintSpawnableComponent))
class HANDLINGRAGDOLLS_API USpineMonitorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USpineMonitorComponent();

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

public:
	// ============================================================
	// Query
	// ============================================================

	/** Get overall stress (0-1) from the monitored source */
	UFUNCTION(BlueprintCallable, Category = "Spine Monitor")
	float GetOverallStress() const;

	/** Set the target actor to monitor (must implement ISpineMonitorable) */
	UFUNCTION(BlueprintCallable, Category = "Spine Monitor")
	void SetMonitorTarget(AActor* Target);

	/** Check if any bone is in warning state */
	UFUNCTION(BlueprintCallable, Category = "Spine Monitor")
	bool IsInWarningState() const { return bIsInWarning; }

	/** Check if spine has failed */
	UFUNCTION(BlueprintCallable, Category = "Spine Monitor")
	bool HasFailed() const { return bHasFailed; }

	// ============================================================
	// Events
	// ============================================================

	/** Fired when a bone enters warning threshold */
	UPROPERTY(BlueprintAssignable, Category = "Spine Monitor|Events")
	FOnSpineWarning OnSpineWarning;

	/** Fired when a bone is at critical stress (near failure) */
	UPROPERTY(BlueprintAssignable, Category = "Spine Monitor|Events")
	FOnSpineCritical OnSpineCritical;

	/** Fired when spine damage exceeds threshold — simulation fail */
	UPROPERTY(BlueprintAssignable, Category = "Spine Monitor|Events")
	FOnSpineFailure OnSpineFailure;

	/** Fired when all bones return to safe levels after being in warning */
	UPROPERTY(BlueprintAssignable, Category = "Spine Monitor|Events")
	FOnSpineSafe OnSpineSafe;

protected:
	/** Critical threshold (0-1). Above this broadcasts OnSpineCritical */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spine Monitor|Config", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CriticalThreshold = 0.85f;

	/** Warning threshold (0-1). Above this broadcasts OnSpineWarning */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spine Monitor|Config", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WarningThreshold = 0.5f;

private:
	/** Cached reference to the spine monitorable interface on the owning actor */
	IISpineMonitorable* MonitorTarget = nullptr;

	bool bIsInWarning = false;
	bool bHasFailed = false;
	bool bWasInWarningLastFrame = false;
};
