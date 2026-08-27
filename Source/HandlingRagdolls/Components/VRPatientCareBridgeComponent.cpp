#include "VRPatientCareBridgeComponent.h"

#include "GrabComponent.h"
#include "EnhancedInputComponent.h"
#include "InputAction.h"
#include "GameFramework/Pawn.h"
#include "MotionControllerComponent.h"

UVRPatientCareBridgeComponent::UVRPatientCareBridgeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UVRPatientCareBridgeComponent::BeginPlay()
{
	Super::BeginPlay();
	TryInstall();
}

void UVRPatientCareBridgeComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	TryInstall();
}

bool UVRPatientCareBridgeComponent::TryInstall()
{
	APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn)
	{
		return false;
	}

	if (!LeftController)
	{
		LeftController = FindController(true);
	}
	if (!RightController)
	{
		RightController = FindController(false);
	}
	if (LeftController && !LeftGrabber)
	{
		LeftGrabber = CreateGrabber(TEXT("PatientCareGrabLeft"), LeftController);
	}
	if (RightController && !RightGrabber)
	{
		RightGrabber = CreateGrabber(TEXT("PatientCareGrabRight"), RightController);
	}

	if (!bInputBound)
	{
		if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(Pawn->InputComponent))
		{
			BindInput(EnhancedInput);
		}
	}

	const bool bReady = LeftGrabber && RightGrabber && bInputBound;
	if (bReady && !bReadyLogged)
	{
		bReadyLogged = true;
		SetComponentTickEnabled(false);
		UE_LOG(LogTemp, Log, TEXT("PatientCareBridge: CNA VR hands and grip actions are ready."));
	}
	return bReady;
}

UMotionControllerComponent* UVRPatientCareBridgeComponent::FindController(bool bLeft) const
{
	TArray<UMotionControllerComponent*> Controllers;
	GetOwner()->GetComponents(Controllers);
	UMotionControllerComponent* Fallback = nullptr;
	const FString Side = bLeft ? TEXT("left") : TEXT("right");
	for (UMotionControllerComponent* Controller : Controllers)
	{
		const FString Description = (Controller->GetName() + TEXT(" ") + Controller->GetTrackingMotionSource().ToString()).ToLower();
		if (!Description.Contains(Side))
		{
			continue;
		}
		Fallback = Controller;
		if (Description.Contains(TEXT("grip")))
		{
			return Controller;
		}
	}
	return Fallback;
}

UGrabComponent* UVRPatientCareBridgeComponent::CreateGrabber(FName Name, UMotionControllerComponent* Controller)
{
	UGrabComponent* Grabber = NewObject<UGrabComponent>(GetOwner(), Name);
	if (Grabber)
	{
		GetOwner()->AddInstanceComponent(Grabber);
		Grabber->SetTraceOrigin(Controller);
		Grabber->RegisterComponent();
	}
	return Grabber;
}

void UVRPatientCareBridgeComponent::BindInput(UEnhancedInputComponent* EnhancedInput)
{
	UInputAction* LeftPressed = LoadObject<UInputAction>(nullptr,
		TEXT("/Game/VRTemplate/Input/Actions/IA_Grab_Left_Pressed.IA_Grab_Left_Pressed"));
	UInputAction* LeftReleased = LoadObject<UInputAction>(nullptr,
		TEXT("/Game/VRTemplate/Input/Actions/IA_Grab_Left_Released.IA_Grab_Left_Released"));
	UInputAction* RightPressed = LoadObject<UInputAction>(nullptr,
		TEXT("/Game/VRTemplate/Input/Actions/IA_Grab_Right_Pressed.IA_Grab_Right_Pressed"));
	UInputAction* RightReleased = LoadObject<UInputAction>(nullptr,
		TEXT("/Game/VRTemplate/Input/Actions/IA_Grab_Right_Released.IA_Grab_Right_Released"));

	if (!LeftPressed || !LeftReleased || !RightPressed || !RightReleased)
	{
		return;
	}

	EnhancedInput->BindAction(LeftPressed, ETriggerEvent::Triggered, this, &UVRPatientCareBridgeComponent::GrabLeft);
	EnhancedInput->BindAction(LeftReleased, ETriggerEvent::Triggered, this, &UVRPatientCareBridgeComponent::ReleaseLeft);
	EnhancedInput->BindAction(RightPressed, ETriggerEvent::Triggered, this, &UVRPatientCareBridgeComponent::GrabRight);
	EnhancedInput->BindAction(RightReleased, ETriggerEvent::Triggered, this, &UVRPatientCareBridgeComponent::ReleaseRight);
	bInputBound = true;
}

void UVRPatientCareBridgeComponent::GrabLeft()
{
	if (LeftGrabber) LeftGrabber->TryGrabRagdoll();
}

void UVRPatientCareBridgeComponent::ReleaseLeft()
{
	if (LeftGrabber) LeftGrabber->ReleaseRagdoll();
}

void UVRPatientCareBridgeComponent::GrabRight()
{
	if (RightGrabber) RightGrabber->TryGrabRagdoll();
}

void UVRPatientCareBridgeComponent::ReleaseRight()
{
	if (RightGrabber) RightGrabber->ReleaseRagdoll();
}
