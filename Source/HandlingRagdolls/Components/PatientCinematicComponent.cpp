// Fill out your copyright notice in the Description page of Project Settings.

#include "PatientCinematicComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Components/StereoLayerComponent.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/KismetRenderingLibrary.h"

UPatientCinematicComponent::UPatientCinematicComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

bool UPatientCinematicComponent::CreateVRFadeOverlay()
{
	if (VRFadeLayer) return true;
	if (!GetOwner() || !GetWorld()) return false;

	// A runtime texture avoids a material/texture asset dependency in the cook.
	VRFadeTexture = UKismetRenderingLibrary::CreateRenderTarget2D(this, 16, 16, RTF_RGBA8, FLinearColor::Transparent);
	if (!VRFadeTexture) return false;
	VRFadeLayer = NewObject<UStereoLayerComponent>(GetOwner(), NAME_None, RF_Transient);
	// UStereoLayerComponent defaults to FaceLocked and a quad shape. Its relative
	// transform is in view space, independent of the patient's movement/rotation.
	VRFadeLayer->SetRelativeLocation(FVector(100.f, 0.f, 0.f));
	VRFadeLayer->SetQuadSize(FVector2D(10000.f, 10000.f));
	VRFadeLayer->SetPriority(MAX_int32);
	VRFadeLayer->bLiveTexture = true;
	VRFadeLayer->bNoAlphaChannel = false;
	VRFadeLayer->bSupportsDepth = false;
	VRFadeLayer->SetTexture(VRFadeTexture);
	VRFadeLayer->RegisterComponent();
	VRFadeLayer->AddTickPrerequisiteComponent(this);
	SetComponentTickEnabled(true);
	UE_LOG(LogTemp, Log, TEXT("PatientCinematic: Using face-locked stereo layer for mobile LDR fade."));
	return true;
}

void UPatientCinematicComponent::UpdateVRFadeOverlay(float Opacity)
{
	if (!VRFadeLayer || !VRFadeTexture) return;
	FLinearColor Color = FadeColor;
	Color.A = FMath::Clamp(Opacity, 0.f, 1.f);
	UKismetRenderingLibrary::ClearRenderTarget2D(this, VRFadeTexture, Color);
}

void UPatientCinematicComponent::DestroyVRFadeOverlay()
{
	SetComponentTickEnabled(false);
	if (VRFadeLayer)
	{
		// DestroyComponent unregisters and removes the native compositor layer.
		VRFadeLayer->DestroyComponent();
		VRFadeLayer = nullptr;
	}
	VRFadeTexture = nullptr;
}

void UPatientCinematicComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!VRFadeLayer || !GetWorld()) return;
	const float Elapsed = GetWorld()->GetTimeSeconds() - FadeStartTime;
	if (CurrentPhase == ECinematicPhase::FadingOut)
	{
		UpdateVRFadeOverlay(FadeOutDuration > KINDA_SMALL_NUMBER ? Elapsed / FadeOutDuration : 1.f);
	}
	else if (CurrentPhase == ECinematicPhase::FadingIn)
	{
		UpdateVRFadeOverlay(FadeInDuration > KINDA_SMALL_NUMBER ? 1.f - Elapsed / FadeInDuration : 0.f);
	}
}

void UPatientCinematicComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CancelCinematic();
	Super::EndPlay(EndPlayReason);
}

void UPatientCinematicComponent::OnUnregister()
{
	CancelCinematic();
	Super::OnUnregister();
}

void UPatientCinematicComponent::Initialize(USkeletalMeshComponent* InMesh, FName InPelvisBoneName)
{
	Mesh = InMesh;
	PelvisBoneName = InPelvisBoneName;
}

// ============================================================
// Public Interface
// ============================================================

void UPatientCinematicComponent::StartCinematicSequence()
{
	if (!bEnabled)
	{
		UE_LOG(LogTemp, Log, TEXT("PatientCinematic: Cinematic is disabled, skipping."));
		return;
	}
	if (CurrentPhase != ECinematicPhase::Idle)
	{
		UE_LOG(LogTemp, Warning, TEXT("PatientCinematic: Sequence already in progress (phase %d), ignoring."),
			static_cast<int32>(CurrentPhase));
		return;
	}

	CurrentPhase = ECinematicPhase::WaitingPreFade;
	UE_LOG(LogTemp, Log, TEXT("PatientCinematic: Starting cinematic sequence. Pre-fade delay: %.1fs"), PreFadeDelay);

	if (PreFadeDelay > KINDA_SMALL_NUMBER)
	{
		GetWorld()->GetTimerManager().SetTimer(
			PreFadeTimerHandle, this, &UPatientCinematicComponent::BeginFadeOut,
			PreFadeDelay, false);
	}
	else
	{
		BeginFadeOut();
	}
}

void UPatientCinematicComponent::CancelCinematic()
{
	if (CurrentPhase == ECinematicPhase::Idle) return;

	UE_LOG(LogTemp, Log, TEXT("PatientCinematic: Cancelling cinematic at phase %d."),
		static_cast<int32>(CurrentPhase));

	ClearAllTimers();
	DestroyVRFadeOverlay();

	// If the screen is faded, restore it immediately.
	if (CurrentPhase == ECinematicPhase::FadingOut ||
		CurrentPhase == ECinematicPhase::HoldingBlack ||
		CurrentPhase == ECinematicPhase::FadingIn)
	{
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
		{
			if (APlayerCameraManager* CameraManager = PC->PlayerCameraManager)
			{
				CameraManager->StopCameraFade();
			}
		}
	}

	CurrentPhase = ECinematicPhase::Idle;
}

// ============================================================
// Fade Sequence Steps
// ============================================================

void UPatientCinematicComponent::BeginFadeOut()
{
	CurrentPhase = ECinematicPhase::FadingOut;
	FadeStartTime = GetWorld()->GetTimeSeconds();
	UE_LOG(LogTemp, Log, TEXT("PatientCinematic: Beginning fade out over %.1fs."), FadeOutDuration);

	const IConsoleVariable* MobileHDR = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MobileHDR"));
	if (GEngine && GEngine->IsStereoscopic3D()
		&& GetWorld()->GetFeatureLevel() <= ERHIFeatureLevel::ES3_1
		&& MobileHDR && MobileHDR->GetInt() == 0)
	{
		if (!CreateVRFadeOverlay())
		{
			UE_LOG(LogTemp, Error, TEXT("PatientCinematic: Failed to create mobile VR fade overlay."));
		}
		UpdateVRFadeOverlay(0.f);
	}

	if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		if (APlayerCameraManager* CameraManager = PC->PlayerCameraManager)
		{
			// Fade from clear (0) to black (1), hold when finished.
			CameraManager->StartCameraFade(0.0f, 1.0f, FadeOutDuration, FadeColor, bFadeAudio, true);
		}
	}

	// Timer fires after FadeOutDuration when the screen is fully black.
	if (FadeOutDuration <= KINDA_SMALL_NUMBER)
	{
		OnFadeOutComplete();
		return;
	}
	GetWorld()->GetTimerManager().SetTimer(
		FadeOutTimerHandle, this, &UPatientCinematicComponent::OnFadeOutComplete,
		FadeOutDuration, false);
}

void UPatientCinematicComponent::OnFadeOutComplete()
{
	CurrentPhase = ECinematicPhase::HoldingBlack;
	UpdateVRFadeOverlay(1.f);
	UE_LOG(LogTemp, Log, TEXT("PatientCinematic: Screen is black. Repositioning patient."));

	// Reposition and swap animation while the screen is black.
	RepositionPatient();

	// Hold the black screen for the configured duration.
	if (BlackScreenHoldDuration > KINDA_SMALL_NUMBER)
	{
		GetWorld()->GetTimerManager().SetTimer(
			BlackScreenTimerHandle, this, &UPatientCinematicComponent::BeginFadeIn,
			BlackScreenHoldDuration, false);
	}
	else
	{
		BeginFadeIn();
	}
}

void UPatientCinematicComponent::BeginFadeIn()
{
	CurrentPhase = ECinematicPhase::FadingIn;
	FadeStartTime = GetWorld()->GetTimeSeconds();
	UpdateVRFadeOverlay(1.f);
	UE_LOG(LogTemp, Log, TEXT("PatientCinematic: Beginning fade in over %.1fs."), FadeInDuration);

	if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		if (APlayerCameraManager* CameraManager = PC->PlayerCameraManager)
		{
			// Fade from black (1) to clear (0), do not hold.
			CameraManager->StartCameraFade(1.0f, 0.0f, FadeInDuration, FadeColor, bFadeAudio, false);
		}
	}

	// Timer fires after FadeInDuration when the screen is fully clear.
	if (FadeInDuration <= KINDA_SMALL_NUMBER)
	{
		OnFadeInComplete();
		return;
	}
	GetWorld()->GetTimerManager().SetTimer(
		FadeInTimerHandle, this, &UPatientCinematicComponent::OnFadeInComplete,
		FadeInDuration, false);
}

void UPatientCinematicComponent::OnFadeInComplete()
{
	ClearAllTimers();
	DestroyVRFadeOverlay();
	CurrentPhase = ECinematicPhase::Idle;
	UE_LOG(LogTemp, Log, TEXT("PatientCinematic: Cinematic sequence complete."));
	OnCinematicComplete.Broadcast();
}

// ============================================================
// Reposition & Animation Swap
// ============================================================

void UPatientCinematicComponent::RepositionPatient()
{
	if (!Mesh) return;

	// 1. Capture current Pelvis transform as our anchor
	FTransform AnchorTransform = Mesh->GetComponentTransform();
	if (!PelvisBoneName.IsNone())
	{
		AnchorTransform = Mesh->GetSocketTransform(PelvisBoneName);
	}

	// 2. Apply user-requested offsets to the Anchor
	AnchorTransform.SetLocation(AnchorTransform.GetLocation() + PostFadeLocationOffset);
	FRotator NewAnchorRot = AnchorTransform.Rotator() + PostFadeRelativeRotation;
	AnchorTransform.SetRotation(NewAnchorRot.Quaternion());

	// 3. Swap animation
	if (PostFadeAnimation)
	{
		Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		Mesh->SetAnimation(PostFadeAnimation);
		Mesh->SetPosition(0.0f);
		Mesh->SetPlayRate(1.0f);
		Mesh->Play(bLoopPostFadeAnimation);
		
		UE_LOG(LogTemp, Log, TEXT("PatientCinematic: Switched to post-fade animation '%s'."), *PostFadeAnimation->GetName());
	}

	// 4. Force evaluate new pose
	Mesh->TickAnimation(0.0f, false);
	Mesh->RefreshBoneTransforms();

		// 5. Align Mesh so new Anim Pelvis matches the Offset Anchor
	if (!PelvisBoneName.IsNone())
	{
		// CORRECT ORDER: Rotate first, then translate.
		FRotator NewAnimPelvisRot = Mesh->GetSocketRotation(PelvisBoneName);
		float YawDiff = AnchorTransform.Rotator().Yaw - NewAnimPelvisRot.Yaw;
		Mesh->AddWorldRotation(FRotator(0.0f, YawDiff, 0.0f), false, nullptr, ETeleportType::TeleportPhysics);

		Mesh->RefreshBoneTransforms();

		FVector NewAnimPelvisLoc = Mesh->GetSocketLocation(PelvisBoneName);
		FVector LocDiff = AnchorTransform.GetLocation() - NewAnimPelvisLoc;
		Mesh->AddWorldOffset(LocDiff, false, nullptr, ETeleportType::TeleportPhysics);
	}
	else
	{
		// Fallback to moving the actor root directly if no pelvis bone
		Mesh->SetWorldLocationAndRotation(AnchorTransform.GetLocation(), AnchorTransform.Rotator(), false, nullptr, ETeleportType::TeleportPhysics);
	}

	UE_LOG(LogTemp, Log, TEXT("PatientCinematic: Repositioned patient."));
}

// ============================================================
// Timer Cleanup
// ============================================================

void UPatientCinematicComponent::ClearAllTimers()
{
	if (UWorld* World = GetWorld())
	{
		FTimerManager& TM = World->GetTimerManager();
		TM.ClearTimer(PreFadeTimerHandle);
		TM.ClearTimer(FadeOutTimerHandle);
		TM.ClearTimer(BlackScreenTimerHandle);
		TM.ClearTimer(FadeInTimerHandle);
	}
}


