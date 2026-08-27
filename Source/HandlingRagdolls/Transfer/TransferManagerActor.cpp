// Fill out your copyright notice in the Description page of Project Settings.

#include "TransferManagerActor.h"
#include "../StateMachine/TransferStateMachine.h"
#include "../Components/ScoringComponent.h"
#include "../Components/SpineMonitorComponent.h"
#include "../Components/BeltComponent.h"
#include "../Components/VRPatientCareBridgeComponent.h"
#include "../Patient/PatientActor.h"
#include "../Interfaces/IPatient.h"
#include "WheelchairActor.h"
#include "BeltActor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"

ATransferManagerActor::ATransferManagerActor()
{
	PrimaryActorTick.bCanEverTick = false;

	// Create the orchestration components
	StateMachine = CreateDefaultSubobject<UTransferStateMachine>(TEXT("TransferStateMachine"));
	Scoring = CreateDefaultSubobject<UScoringComponent>(TEXT("Scoring"));
	SpineMonitor = CreateDefaultSubobject<USpineMonitorComponent>(TEXT("SpineMonitor"));
}

void ATransferManagerActor::BeginPlay()
{
	Super::BeginPlay();

	// Level actor references can be invalidated when the migrated setup is
	// regenerated or an actor is replaced. Recover the native transfer actors
	// by class so the state machine never starts with a missing belt reference.
	if (!PatientRef)
	{
		for (TActorIterator<APatientActor> It(GetWorld()); It; ++It)
		{
			PatientRef = *It;
			break;
		}
	}
	if (!WheelchairRef)
	{
		for (TActorIterator<AWheelchairActor> It(GetWorld()); It; ++It)
		{
			WheelchairRef = *It;
			break;
		}
	}
	if (!BeltRef)
	{
		for (TActorIterator<ABeltActor> It(GetWorld()); It; ++It)
		{
			BeltRef = *It;
			break;
		}
	}

	if (bAutoConfigureVRHands && GetWorld())
	{
		APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
		if (APawn* PlayerPawn = PlayerController ? PlayerController->GetPawn() : nullptr)
		{
			VRPatientCareBridge = NewObject<UVRPatientCareBridgeComponent>(PlayerPawn, TEXT("PatientCareVRBridge"));
			if (VRPatientCareBridge)
			{
				PlayerPawn->AddInstanceComponent(VRPatientCareBridge);
				VRPatientCareBridge->RegisterComponent();
			}
		}
	}

	// Wire the state machine references from this actor's level-set references.
	if (StateMachine)
	{
		UBeltComponent* BeltCompRef = nullptr;
		if (BeltRef)
		{
			BeltCompRef = BeltRef->GetBeltComponent();
		}

		StateMachine->Setup(
			TScriptInterface<IIPatient>(PatientRef),
			WheelchairRef,
			BeltCompRef,
			Scoring
		);
	}

	// Wire the spine monitor to watch the patient
	if (SpineMonitor && PatientRef)
	{
		SpineMonitor->SetMonitorTarget(PatientRef);
	}

	if (bAutoStart)
	{
		StartTransfer();
	}

	UE_LOG(LogTemp, Log, TEXT("TransferManager: Ready. Patient=%s, Wheelchair=%s, Belt=%s"),
		*GetNameSafe(PatientRef), *GetNameSafe(WheelchairRef), *GetNameSafe(BeltRef));
}

void ATransferManagerActor::StartTransfer()
{
	if (StateMachine)
	{
		StateMachine->StartTask();
		UE_LOG(LogTemp, Log, TEXT("TransferManager: Transfer task started."));
	}
}

void ATransferManagerActor::ResetTransfer()
{
	if (StateMachine)
	{
		StateMachine->Reset();
	}
	if (Scoring)
	{
		Scoring->ResetScore();
	}
	UE_LOG(LogTemp, Log, TEXT("TransferManager: Transfer reset."));
}

FText ATransferManagerActor::GetCurrentInstructions() const
{
	if (StateMachine)
	{
		return StateMachine->GetCurrentInstructions();
	}
	return FText::FromString(TEXT("Transfer Manager not initialized."));
}
