#include "PatientCareProbeComponent.h"
#include "PatientActor.h"
#include "../Components/GrabComponent.h"
#include "../Components/PatientBedSupportComponent.h"
#include "../Components/PatientPhysicsComponent.h"
#include "../Components/SeatedTransitionComponent.h"
#include "../Components/BeltComponent.h"
#include "../Transfer/BeltActor.h"
#include "../Transfer/WheelchairActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneComponent.h"
#include "PhysicsEngine/PhysicalAnimationComponent.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/BodyInstance.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommandWithWorld ViewBed(TEXT("cna.Patient.ViewBed"),
    TEXT("Set a desktop inspection camera beside the actual bed. Explicit test command only."),
    FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
    {
        for (TActorIterator<APatientActor> It(World); It; ++It)
        {
            UPatientBedSupportComponent* Support = It->GetBedSupportComponent();
            if (!Support || !Support->BedActor || !World->GetFirstPlayerController()) return;
            const FTransform Bed = Support->BedActor->GetActorTransform();
            const FVector Position = Bed.TransformPosition(FVector(150, -380, 215));
            const FVector Target = Bed.TransformPosition(FVector(140, -150, 125));
            ACameraActor* Camera = World->SpawnActor<ACameraActor>(Position, (Target - Position).Rotation());
            Camera->GetCameraComponent()->SetFieldOfView(65.0f);
            World->GetFirstPlayerController()->SetViewTarget(Camera);
            return;
        }
    }));
static FAutoConsoleCommandWithWorld BedProbe(TEXT("cna.Patient.SmokeBed20"),
	TEXT("Run 20 scripted bed attempts on the active patient, transfer to its chair, then exit. Test instance only."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		for (TActorIterator<APatientActor> It(World); It; ++It)
		{
			if (It->FindComponentByClass<UPatientCareProbeComponent>()) return;
			UPatientCareProbeComponent* Probe = NewObject<UPatientCareProbeComponent>(*It);
			It->AddInstanceComponent(Probe); Probe->RegisterComponent(); Probe->Start(); return;
		}
		UE_LOG(LogTemp, Error, TEXT("PatientProbe: FAIL no native patient in this map."));
		FPlatformMisc::RequestExitWithStatus(false, 1);
	}));
#endif

UPatientCareProbeComponent::UPatientCareProbeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

bool UPatientCareProbeComponent::Check(bool Condition, const TCHAR* Description)
{
	if (!Condition)
	{
		UE_LOG(LogTemp, Error, TEXT("PatientProbe: FAIL attempt=%d stage=%d %s"), Attempt + 1, Stage, Description);
		Finish(false);
	}
	return Condition;
}

void UPatientCareProbeComponent::Finish(bool Passed)
{
	bFinished = true;
	for (UGrabComponent* Hand : Hands) if (Hand) Hand->ReleaseRagdoll();
	UE_LOG(LogTemp, Display, TEXT("PatientProbe: %s completed=%d/20; scripted runtime validation, physical controllers not exercised."), Passed ? TEXT("PASS") : TEXT("FAIL"), Attempt);
	SetComponentTickEnabled(false);
	FPlatformMisc::RequestExitWithStatus(false, Passed ? 0 : 1);
}

void UPatientCareProbeComponent::Start()
{
	Patient = Cast<APatientActor>(GetOwner());
	for (TActorIterator<ABeltActor> It(GetWorld()); It; ++It) Belt = *It;
	for (TActorIterator<AWheelchairActor> It(GetWorld()); It; ++It)
		if (It->GetFName() == TEXT("WheelchairActor_2")) Chair = *It;
	if (!Check(Patient && Belt && Chair && Patient->GetBedSupportComponent()->IsBedControlActive(), TEXT("active map references"))) return;
	InitialTransform = Patient->GetActorTransform();
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		TArray<UGrabComponent*> Existing; It->GetComponents(Existing);
		for (UGrabComponent* Hand : Existing) Hand->CancelGrab();
	}
	for (int32 I = 0; I < 2; ++I)
	{
		AActor* Actor = GetWorld()->SpawnActor<AActor>();
		USceneComponent* Origin = NewObject<USceneComponent>(Actor);
		Actor->SetRootComponent(Origin); Actor->AddInstanceComponent(Origin); Origin->RegisterComponent(); Origins.Add(Origin);
		UGrabComponent* Hand = NewObject<UGrabComponent>(Actor);
		Actor->AddInstanceComponent(Hand); Hand->SetTraceOrigin(Origin); Hand->RegisterComponent(); Hands.Add(Hand);
	}
	const FTransform Bed = Patient->GetBedSupportComponent()->BedActor->GetActorTransform();
	HipTarget = Bed.TransformPosition(FVector(140, -217, 125));
	ChestTarget = HipTarget + FVector(0, 0, 50);
	FootTarget = HipTarget + Bed.TransformVector(FVector(0, -65, 0));
	BeginStage(0, 1.0f); SetComponentTickEnabled(true);
	UE_LOG(LogTemp, Display, TEXT("PatientProbe: START actual patient=%s physics=%s chair=%s"), *Patient->GetName(), *GetNameSafe(Patient->GetPatientMesh()->GetPhysicsAsset()), *Chair->GetName());
}

bool UPatientCareProbeComponent::Acquire(int32 Hand, FName Bone)
{
	Hands[Hand]->ReleaseRagdoll();
	FBodyInstance* Body = Patient->GetPatientMesh()->GetBodyInstance(Bone);
	const bool bFoot = Bone == TEXT("foot_l") || Bone == TEXT("foot_r");
	Origins[Hand]->SetWorldLocation(bFoot && Body ? Body->GetCOMPosition() : Patient->GetPatientMesh()->GetBoneLocation(Bone));
	return Check(Hands[Hand]->TryGrabRagdoll(), TEXT("body acquisition"));
}

void UPatientCareProbeComponent::BeginStage(int32 Next, float Seconds)
{
	Stage = Next; Elapsed = 0; Duration = Seconds;
	for (int32 I = 0; I < 2; ++I) Starts[I] = Targets[I] = Origins[I]->GetComponentLocation();
}

void UPatientCareProbeComponent::ResetBed()
{
	for (UGrabComponent* Hand : Hands) Hand->ReleaseRagdoll();
	USkeletalMeshComponent* Mesh = Patient->GetPatientMesh();
	Patient->GetBedSupportComponent()->SuspendForTransfer();
	Patient->GetPhysicalAnimationComponent()->ApplyPhysicalAnimationSettingsBelow(TEXT("pelvis"), FPhysicalAnimationData());
	Mesh->SetAllBodiesSimulatePhysics(false); Mesh->SetSimulatePhysics(false);
	Mesh->SetAllBodiesPhysicsBlendWeight(0, false);
	Mesh->SetWorldTransform(InitialTransform, false, nullptr, ETeleportType::TeleportPhysics);
	Patient->GetPatientPhysicsComponent()->ApplyRestPose();
	Mesh->TickAnimation(0, false); Mesh->RefreshBoneTransforms();
	Patient->GetBedSupportComponent()->ResumeOnBed();
	Patient->SetPatientState(EPatientState::LyingDown);
	BeginStage(0, 1.0f);
}

void UPatientCareProbeComponent::TickComponent(float DeltaTime, ELevelTick Type, FActorComponentTickFunction* Function)
{
	Super::TickComponent(DeltaTime, Type, Function);
	if (bFinished || Hands.Num() != 2) return;
	if (!Check(!Patient->GetPelvisLocation().ContainsNaN() && Patient->GetPelvisVelocity().Size() < 1800, TEXT("bounded patient motion"))) return;
	Elapsed += DeltaTime;
	float Alpha = FMath::Clamp(Elapsed / Duration, 0.0f, 1.0f);
	for (int32 I = 0; I < 2; ++I) Origins[I]->SetWorldLocation(FMath::Lerp(Starts[I], Targets[I], Alpha));
	if (Elapsed < Duration) return;
	switch (Stage)
	{
	case 0:
		if (!Acquire(0, TEXT("spine_05")) || !Acquire(1, TEXT("pelvis"))) return;
		BeginStage(1, 4); Targets[0] = ChestTarget; Targets[1] = HipTarget; break;
	case 1:
		if (!Acquire(1, TEXT("foot_l"))) return;
		BeginStage(2, 2.5f); Targets[1] = FootTarget + FVector(0, 0, 15); break;
	case 2: BeginStage(3, 2); Targets[1] = FootTarget - FVector(0, 0, 75); break;
	case 3:
		if (!Acquire(1, TEXT("foot_r"))) return;
		BeginStage(4, 2.5f); Targets[1] = FootTarget + FVector(0, 0, 15); break;
	case 4: BeginStage(5, 2); Targets[1] = FootTarget - FVector(0, 0, 75); break;
	case 5: Hands[1]->ReleaseRagdoll(); BeginStage(6, 2); break;
	case 6:
		if (!Check(Patient->IsBedSeated(), TEXT("stable guided edge posture"))) return;
		Hands[0]->ReleaseRagdoll(); BeginStage(7, 2); break;
	case 7:
		if (!Check(Patient->IsBedSeated(), TEXT("released seated balance"))) return;
		++Attempt;
		UE_LOG(LogTemp, Display, TEXT("PatientProbe: attempt %d/20 PASS pelvis=%s angle=%.1f"), Attempt, *Patient->GetPelvisLocation().ToString(), Patient->GetSeatedTransitionComponent()->GetTorsoUprightAngleDeg());
		if (Attempt < 20) { ResetBed(); break; }
		if (!Check(Belt->GetBeltComponent()->AttachToPatient(Patient), TEXT("belt attachment")) || !Acquire(0, TEXT("foot_r"))) return;
		Origins[1]->SetWorldLocation(Belt->GetHandleWorldLocation());
		if (!Check(Hands[1]->TryGrabRagdoll() && Hands[1]->GetGrabbedActor() == Belt && Patient->IsKinematicCarryActive()
			&& !Hands[0]->IsGrabbing() && !Patient->GetBedSupportComponent()->IsBedControlActive(), TEXT("body-to-belt ownership"))) return;
		BeginStage(8, 0.5f); break;
	case 8:
		BeginStage(9, 3); Targets[1] += Chair->GetTargetSeatTransform().GetLocation() - Patient->GetPelvisLocation(); break;
	case 9: BeginStage(10, 1); break;
	case 10: Hands[1]->ReleaseRagdoll(); BeginStage(11, 1); break;
	case 11:
		if (!Check(Chair->IsOccupied() && Patient->GetSeatedTransitionComponent()->IsSeatedLocked()
			&& FVector::Dist(Patient->GetPelvisLocation(), Chair->GetTargetSeatTransform().GetLocation()) < 1, TEXT("selected chair final animation"))) return;
		Finish(true); break;
	}
}
