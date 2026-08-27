// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PatientCinematicComponent.generated.h"

class USkeletalMeshComponent;
class UAnimSequence;

/** Phase tracking for the cinematic fade sequence. */
UENUM(BlueprintType)
enum class ECinematicPhase : uint8
{
	Idle,
	WaitingPreFade,
	FadingOut,
	HoldingBlack,
	FadingIn
};

/** Broadcast when the full cinematic sequence (fade in) completes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCinematicComplete);

/**
 * Manages a timed fade-out → reposition → fade-in cinematic sequence.
 *
 * Intended to run after the patient's bed seated blend completes. Listens
 * for OnBedSeatedBlendComplete from USeatedTransitionComponent (wired in
 * PatientActor::BeginPlay) and orchestrates the screen fade using
 * APlayerCameraManager::StartCameraFade.
 */
UCLASS(ClassGroup = (PatientCare), meta = (BlueprintSpawnableComponent))
class HANDLINGRAGDOLLS_API UPatientCinematicComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPatientCinematicComponent();

	// ============================================================
	// Public Interface
	// ============================================================

	/** Entry point — starts the pre-fade delay timer. */
	UFUNCTION(BlueprintCallable, Category = "Patient Cinematic")
	void StartCinematicSequence();

	/** Cancels the cinematic at any phase, restoring the screen if mid-fade. */
	UFUNCTION(BlueprintCallable, Category = "Patient Cinematic")
	void CancelCinematic();

	/** Current phase of the cinematic sequence. */
	UFUNCTION(BlueprintPure, Category = "Patient Cinematic")
	ECinematicPhase GetCurrentPhase() const { return CurrentPhase; }

	/** Whether the cinematic is currently running (any phase except Idle). */
	UFUNCTION(BlueprintPure, Category = "Patient Cinematic")
	bool IsCinematicActive() const { return CurrentPhase != ECinematicPhase::Idle; }

	// ============================================================
	// Configuration
	// ============================================================

	/** Master enable for the cinematic sequence. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patient Cinematic|Config")
	bool bEnabled = true;

	/** Seconds to wait after bed blend completes before starting the fade. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patient Cinematic|Timing", meta = (ClampMin = "0.0"))
	float PreFadeDelay = 3.0f;

	/** Seconds for the screen to fade from clear to black. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patient Cinematic|Timing", meta = (ClampMin = "0.1"))
	float FadeOutDuration = 1.5f;

	/** Seconds to hold the black screen (reposition happens at start of this). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patient Cinematic|Timing", meta = (ClampMin = "0.0"))
	float BlackScreenHoldDuration = 1.0f;

	/** Seconds for the screen to fade from black to clear. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patient Cinematic|Timing", meta = (ClampMin = "0.1"))
	float FadeInDuration = 1.5f;

	/** Animation to play after repositioning (during black screen). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patient Cinematic|Animation")
	TObjectPtr<UAnimSequence> PostFadeAnimation;

	/** Whether the post-fade animation loops. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patient Cinematic|Animation")
	bool bLoopPostFadeAnimation = true;

	/** Rotation applied to the patient during the black screen (relative to current). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patient Cinematic|Reposition")
	FRotator PostFadeRelativeRotation = FRotator(0.0f, 180.0f, 0.0f);

	/** Optional position offset applied during the black screen (relative to current, world space). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patient Cinematic|Reposition")
	FVector PostFadeLocationOffset = FVector::ZeroVector;

	/** Color of the fade. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patient Cinematic|Config")
	FLinearColor FadeColor = FLinearColor::Black;

	/** Whether to also fade audio during the screen fade. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patient Cinematic|Config")
	bool bFadeAudio = false;

	// ============================================================
	// Events
	// ============================================================

	/** Broadcast when the full cinematic sequence finishes. */
	UPROPERTY(BlueprintAssignable, Category = "Patient Cinematic|Events")
	FOnCinematicComplete OnCinematicComplete;

	/** Set the mesh reference (called from PatientActor::BeginPlay). */
	void Initialize(USkeletalMeshComponent* InMesh, FName InPelvisBoneName);

private:
	void BeginFadeOut();
	void OnFadeOutComplete();
	void BeginFadeIn();
	void OnFadeInComplete();
	void RepositionPatient();
	void ClearAllTimers();

	ECinematicPhase CurrentPhase = ECinematicPhase::Idle;

	FTimerHandle PreFadeTimerHandle;
	FTimerHandle FadeOutTimerHandle;
	FTimerHandle BlackScreenTimerHandle;
	FTimerHandle FadeInTimerHandle;

	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> Mesh;
	FName PelvisBoneName;
};



