// Fill out your copyright notice in the Description page of Project Settings.

#include "PatientCinematicComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "GameFramework/Pawn.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "../Patient/PatientActor.h"
#include "AudioDevice.h"

UPatientCinematicComponent::UPatientCinematicComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FadeMaterial(TEXT("/Game/Project/Custom_Materails/M_VRFade"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere"));
	FadeSphereMaterial = FadeMaterial.Object;
	FadeSphereMesh = Sphere.Object;
}

bool UPatientCinematicComponent::CreateVRFadeOverlay()
{
	if (VRFadeSphere) return true;
	APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
	UCameraComponent* Camera = Pawn ? Pawn->FindComponentByClass<UCameraComponent>() : nullptr;
	if (!Camera || !FadeSphereMaterial || !FadeSphereMesh) return false;

	// Use the same camera-space geometry/material as the proven menu FadeSphere,
	// but never share its MID, timeline or level-transition delegates.
	VRFadeSphere = NewObject<UStaticMeshComponent>(Pawn, NAME_None, RF_Transient);
	Pawn->AddInstanceComponent(VRFadeSphere);
	VRFadeSphere->SetStaticMesh(FadeSphereMesh);
	VRFadeSphere->SetupAttachment(Camera);
	VRFadeSphere->SetRelativeTransform(FTransform::Identity);
	VRFadeSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VRFadeSphere->SetGenerateOverlapEvents(false);
	VRFadeSphere->SetCanEverAffectNavigation(false);
	VRFadeSphere->SetCastShadow(false);
	VRFadeSphere->TranslucencySortPriority = 10000;
	VRFadeMaterial = UMaterialInstanceDynamic::Create(FadeSphereMaterial, this);
	VRFadeMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.f);
	VRFadeSphere->SetMaterial(0, VRFadeMaterial);
	VRFadeSphere->RegisterComponent();
	SetComponentTickEnabled(true);
	UE_LOG(LogTemp, Display, TEXT("PatientCinematic: MeshFade camera=%s material=%s stereo=%d featureLevel=%d"),
		*Camera->GetPathName(), *FadeSphereMaterial->GetPathName(), GEngine && GEngine->IsStereoscopic3D(), (int32)GetWorld()->GetFeatureLevel());
	return true;
}

void UPatientCinematicComponent::UpdateVRFadeOverlay(float Opacity)
{
	if (VRFadeMaterial) VRFadeMaterial->SetScalarParameterValue(TEXT("Opacity"), FMath::Clamp(Opacity, 0.f, 1.f));
	if (VRFadeSphere && bFadeAudio && GetWorld())
		if (FAudioDeviceHandle Audio = GetWorld()->GetAudioDevice()) Audio->SetTransientPrimaryVolume(1.f - FMath::Clamp(Opacity, 0.f, 1.f));
}

void UPatientCinematicComponent::DestroyVRFadeOverlay()
{
	SetComponentTickEnabled(false);
	bRepositionPending = false;
	UpdateVRFadeOverlay(0.f);
	if (VRFadeSphere)
	{
		VRFadeSphere->DestroyComponent();
		VRFadeSphere = nullptr;
	}
	VRFadeMaterial = nullptr;
}

void UPatientCinematicComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!GetWorld()) return;
	if (bRepositionPending && GFrameCounter > BlackFrameNumber)
	{
		RepositionAfterBlackFrame();
		return;
	}
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
	if (CurrentPhase == ECinematicPhase::Idle)
	{
		ClearAllTimers();
		DestroyVRFadeOverlay();
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("PatientCinematic: Cancelling cinematic at phase %d."),
		static_cast<int32>(CurrentPhase));

	ClearAllTimers();
	DestroyVRFadeOverlay();

	// If the screen is faded, restore it immediately.
	if (bOwnsCameraFade && (CurrentPhase == ECinematicPhase::FadingOut ||
		CurrentPhase == ECinematicPhase::HoldingBlack ||
		CurrentPhase == ECinematicPhase::FadingIn))
	{
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
		{
			if (APlayerCameraManager* CameraManager = PC->PlayerCameraManager)
			{
				CameraManager->StopCameraFade();
			}
		}
	}

	bOwnsCameraFade = false;
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

	const bool bUseMeshFade = bUseMeshFadeWithoutHMD || PLATFORM_ANDROID || (GEngine && GEngine->IsStereoscopic3D());
	if (bUseMeshFade)
	{
		if (!CreateVRFadeOverlay())
		{
			UE_LOG(LogTemp, Error, TEXT("PatientCinematic: No camera/material for mesh fade; cancelling before reposition."));
			CancelCinematic();
			OnCinematicComplete.Broadcast();
			return;
		}
		UpdateVRFadeOverlay(0.f);
	}

	if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		if (APlayerCameraManager* CameraManager = PC->PlayerCameraManager)
		{
			if (!VRFadeSphere)
			{
				bOwnsCameraFade = true;
				CameraManager->StartCameraFade(0.0f, 1.0f, FadeOutDuration, FadeColor, bFadeAudio, true);
			}
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
	if (VRFadeSphere)
	{
		BlackFrameNumber = GFrameCounter;
		bRepositionPending = true;
		return;
	}
	RepositionAfterBlackFrame();
}

void UPatientCinematicComponent::RepositionAfterBlackFrame()
{
	bRepositionPending = false;
	UE_LOG(LogTemp, Display, TEXT("PatientCinematic: Full black frame reached. Repositioning patient."));

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
			if (!VRFadeSphere) CameraManager->StartCameraFade(1.0f, 0.0f, FadeInDuration, FadeColor, bFadeAudio, false);
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
	bOwnsCameraFade = false;
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

#if !UE_BUILD_SHIPPING
// Uses the same backend selector, timings and frame gate, with no patient mesh
// and no menu timeline/delegate. Safe to run repeatedly on a development Quest.
static FAutoConsoleCommandWithWorld GCNATestPatientFade(
	TEXT("cna.Patient.TestFade"), TEXT("Test the patient fade without moving the patient or loading a level."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0);
		if (!Pawn) return;
		const FName Name(TEXT("PatientFadeDiagnostic"));
		UPatientCinematicComponent* Test = FindObject<UPatientCinematicComponent>(Pawn, *Name.ToString());
		if (!Test)
		{
			Test = NewObject<UPatientCinematicComponent>(Pawn, Name, RF_Transient);
			Pawn->AddInstanceComponent(Test); Test->RegisterComponent();
		}
		Test->PreFadeDelay = 0.f;
		Test->bUseMeshFadeWithoutHMD = true;
		Test->StartCinematicSequence();
	}));
#endif


