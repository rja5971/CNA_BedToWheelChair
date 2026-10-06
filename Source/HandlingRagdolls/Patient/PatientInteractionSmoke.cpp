#include "PatientActor.h"
#include "../Components/GrabComponent.h"
#include "../Components/BeltComponent.h"
#include "../Components/PatientCinematicComponent.h"
#include "../Components/PatientCarryComponent.h"
#include "../Components/SeatedTransitionComponent.h"
#include "../Transfer/BeltActor.h"
#include "../Transfer/WheelchairActor.h"
#include "../Transfer/TransferManagerActor.h"
#include "../StateMachine/States/WheelchairTransferState.h"
#include "../Reliability/CNAReliabilityLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/SceneComponent.h"
#include "Blueprint/UserWidget.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/PhysicsHandleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "HAL/IConsoleManager.h"

#if !UE_BUILD_SHIPPING
namespace
{
struct FPatientInteractionSmoke : TSharedFromThis<FPatientInteractionSmoke>
{
    UWorld* World = nullptr;
    APatientActor* Patient = nullptr;
    ABeltActor* Belt = nullptr;
    AWheelchairActor* Chair = nullptr;
    ATransferManagerActor* Manager = nullptr;
    UGrabComponent* Hands[2] = {};
    USceneComponent* Origins[2] = {};
    FTimerHandle Poll;
    bool bPassed = true;
    float Deadline = 0;
    int32 BlockedAttempts = 0;

    void Check(bool bCondition, const TCHAR* Message)
    {
        bPassed &= bCondition;
        UE_LOG(LogTemp, Display, TEXT("PATIENT_INTERACTION_SMOKE %s: %s"), Message, bCondition ? TEXT("PASS") : TEXT("FAIL"));
    }
    void Finish()
    {
        UE_LOG(LogTemp, Display, TEXT("PATIENT_INTERACTION_SMOKE RESULT: %s"), bPassed ? TEXT("PASS") : TEXT("FAIL"));
        FPlatformMisc::RequestExitWithStatus(false, bPassed ? 0 : 1);
    }
    void Move(int32 Index, FVector Location) { Origins[Index]->SetWorldLocation(Location); }
    FVector Head() const
    {
        FBodyInstance* Body = Patient->GetPatientMesh()->GetBodyInstance(TEXT("head"));
        return Body ? Body->GetCOMPosition() : Patient->GetPatientMesh()->GetBoneLocation(TEXT("head"));
    }
    bool Grab(int32 Index, AActor* Expected)
    {
        return Hands[Index]->TryGrabRagdoll() && Hands[Index]->GetGrabbedActor() == Expected;
    }
    bool HandlesClear() const
    {
        for (int32 Index = 0; Index < 2; ++Index)
        {
            if (Hands[Index]->IsGrabbing()) return false;
            TArray<UPhysicsHandleComponent*> Handles; Hands[Index]->GetOwner()->GetComponents(Handles);
            for (UPhysicsHandleComponent* Handle : Handles) if (Handle->GetGrabbedComponent()) return false;
        }
        return true;
    }
    void Start()
    {
        for (TActorIterator<APatientActor> It(World); It; ++It) { Patient = *It; break; }
        for (TActorIterator<ABeltActor> It(World); It; ++It)
            if (It->GetInteractionPatient() == Patient) { Belt = *It; break; }
        for (TActorIterator<AWheelchairActor> It(World); It; ++It) { Chair = *It; break; }
        for (TActorIterator<ATransferManagerActor> It(World); It; ++It) { Manager = *It; break; }
        Check(Patient && Belt && Chair && Manager, TEXT("Actual training actors bound"));
        if (!bPassed) { Finish(); return; }
        // Freeze task progression only in this test process; pose/cinematic/carry components remain live.
        Manager->ResetTransfer();
        for (TActorIterator<AActor> It(World); It; ++It)
            if (It->GetClass()->GetName() == TEXT("BP_PatientChat_C"))
            {
                UCNAReliabilityLibrary::CallNoArgs(*It, TEXT("EnableChatPanel"));
                UWidgetComponent* Panel = Cast<UWidgetComponent>(UCNAReliabilityLibrary::ObjectProperty(*It, TEXT("ChatPanel")));
                Check(Panel && Panel->GetUserWidgetObject() && !Panel->IsVisible()
                    && Panel->GetCollisionEnabled() == ECollisionEnabled::NoCollision
                    && !Panel->GetUserWidgetObject()->GetIsEnabled(), TEXT("Room screen retains widget but cannot render or receive input"));
            }
        for (int32 Index = 0; Index < 2; ++Index)
        {
            AActor* HandActor = World->SpawnActor<AActor>();
            Origins[Index] = NewObject<USceneComponent>(HandActor); HandActor->SetRootComponent(Origins[Index]);
            HandActor->AddInstanceComponent(Origins[Index]); Origins[Index]->RegisterComponent();
            Hands[Index] = NewObject<UGrabComponent>(HandActor); HandActor->AddInstanceComponent(Hands[Index]);
            Hands[Index]->SetTraceOrigin(Origins[Index]); Hands[Index]->RegisterComponent();
            Move(Index, Head());
            Check(Grab(Index, Patient), TEXT("Original head contact acquired"));
        }
        Hands[1]->ReleaseRagdoll();
        Check(Hands[0]->IsGrabbing() && Patient->IsNeckSupported(), TEXT("Releasing one hand retains other head support"));
        Move(1, Belt->GetActorLocation());
        Check(Grab(1, Belt), TEXT("Loose belt acquired before seating"));
        Patient->SetPatientState(EPatientState::Seated);
        Check(Patient->GetInteractionPhase() == EPatientInteractionPhase::BedSeating && HandlesClear(), TEXT("Upright seating cancels body and loose belt before fade"));
        Check(!Belt->GetBeltComponent()->HasPendingFinalHandleRelease(), TEXT("Forced release does not request wheelchair seating"));
        UPatientCinematicComponent* Cinematic = Patient->GetPatientCinematicComponent();
        Check(Cinematic->GetCurrentPhase() == ECinematicPhase::WaitingPreFade, TEXT("Original pre-fade timer still starts"));
        Deadline = World->GetTimeSeconds() + Cinematic->PreFadeDelay + Cinematic->FadeOutDuration
            + Cinematic->BlackScreenHoldDuration + Cinematic->FadeInDuration + 5.f;
        TSharedRef<FPatientInteractionSmoke> Self = AsShared();
        World->GetTimerManager().SetTimer(Poll, [Self]() { Self->DuringFade(); }, .1f, true);
    }
    void DuringFade()
    {
        // Clearing the polling timer releases its captured owner. Keep the test
        // alive until the next stage has installed its own timer.
        TSharedRef<FPatientInteractionSmoke> KeepAlive = AsShared();
        if (Patient->GetInteractionPhase() == EPatientInteractionPhase::BeltTransfer)
        {
            World->GetTimerManager().ClearTimer(Poll);
            AfterFade(); return;
        }
        Move(0, Head()); Move(1, Belt->GetActorLocation());
        bPassed &= !Hands[0]->TryGrabRagdoll() && !Hands[1]->TryGrabRagdoll() && HandlesClear();
        ++BlockedAttempts;
        if (World->GetTimeSeconds() > Deadline)
        {
            Check(false, TEXT("Fade completed within configured durations"));
            World->GetTimerManager().ClearTimer(Poll); Finish();
        }
    }
    void AfterFade()
    {
        Check(BlockedAttempts > 0 && HandlesClear(), TEXT("Repeated held-grip attempts stayed blocked across fade"));
        Check(!Patient->GetPatientCinematicComponent()->IsCinematicActive(), TEXT("Fade and rotation complete normally"));
        Move(0, Head()); Check(!Hands[0]->TryGrabRagdoll(), TEXT("Rotated patient rejects head grabs"));
        Check(Belt->GetBeltComponent()->AttachToPatient(Patient), TEXT("Existing belt attachment succeeds after rotation"));
        for (int32 Index = 0; Index < 2; ++Index)
        {
            Move(Index, Belt->GetHandleWorldLocation()); Check(Grab(Index, Belt), TEXT("Attached front handle acquired"));
        }
        Check(Patient->IsKinematicCarryActive() && Belt->GetBeltComponent()->GetActiveGrabCount() == 2, TEXT("Existing two-hand carry starts"));
        Hands[0]->ReleaseRagdoll(); Belt->GetBeltComponent()->OnHandleReleased(Hands[0]);
        Check(Patient->IsKinematicCarryActive() && Belt->GetBeltComponent()->GetActiveGrabCount() == 1
            && !Belt->GetBeltComponent()->HasPendingFinalHandleRelease(), TEXT("Single and duplicate releases cannot steal second hand or arm seating"));
        Move(0, Belt->GetHandleWorldLocation()); Check(Grab(0, Belt), TEXT("Released hand can re-grab belt"));
        Patient->SetInteractionPhase(EPatientInteractionPhase::WheelchairSeating);
        Check(HandlesClear() && !Patient->IsKinematicCarryActive() && Belt->GetBeltComponent()->GetActiveGrabCount() == 0
            && !Belt->GetBeltComponent()->HasPendingFinalHandleRelease(), TEXT("Forced two-hand release clears carry without seating gesture"));
        TSharedRef<FPatientInteractionSmoke> Self = AsShared(); FTimerHandle Wait;
        World->GetTimerManager().SetTimer(Wait, [Self]() { Self->ChairChecks(); }, .35f, false);
    }
    void ChairChecks()
    {
        Check(!Patient->GetPatientMesh()->IsSimulatingPhysics(), TEXT("Cancellation does not wake physics after carry grace period"));
        USeatedTransitionComponent* Transition = Patient->GetSeatedTransitionComponent();
        UAnimSequence* Animation = Transition->SeatedAnimation;
        Patient->SetInteractionPhase(EPatientInteractionPhase::BeltTransfer);
        Move(0, Belt->GetHandleWorldLocation()); Check(Grab(0, Belt), TEXT("Belt re-grab after rejected seating"));
        Hands[0]->ReleaseRagdoll();
        Chair->LockBrakes();
        UWheelchairTransferState* State = NewObject<UWheelchairTransferState>(Manager);
        State->EnterState(Manager->GetStateMachine());
        Patient->GetPatientMesh()->AddWorldOffset(Chair->GetTargetSeatTransform().GetLocation() - Patient->GetPelvisLocation(), false, nullptr, ETeleportType::TeleportPhysics);
        Patient->GetPatientMesh()->RefreshBoneTransforms();
        Transition->SeatedAnimation = nullptr;
        // Exercise failure through the real transfer state; it must not report completion.
        State->TickState(0.f);
        Transition->SeatedAnimation = Animation;
        Check(!State->CanTransitionToNext() && !Chair->IsOccupied() && Belt->IsGrabInteractionEnabled()
            && !Patient->IsGrabInteractionEnabled(), TEXT("Invalid seating animation leaves task incomplete and restores belt-only retry"));
        Move(0, Belt->GetHandleWorldLocation()); Check(Grab(0, Belt), TEXT("Belt re-grab after seating validation failure"));
        Hands[0]->ReleaseRagdoll();
        Check(Belt->GetBeltComponent()->HasPendingFinalHandleRelease(), TEXT("User final release arms retry exactly once"));
        State->TickState(0.f); State->TickState(0.f);
        Check(State->CanTransitionToNext() && !Belt->GetBeltComponent()->ConsumeFinalHandleRelease(), TEXT("Existing release-driven chair handoff succeeds and consumes gesture once"));
        USceneComponent* SeatParent = Patient->GetPatientMesh()->GetAttachParent();
        AWheelchairActor* SelectedChair = SeatParent ? Cast<AWheelchairActor>(SeatParent->GetOwner()) : nullptr;
        Check(Patient->GetInteractionPhase() == EPatientInteractionPhase::Complete && SelectedChair
            && SelectedChair->IsOccupied() && SeatParent == SelectedChair->GetSeatTargetComponent(),
            TEXT("Successful selected-chair seating locks completed simulation"));
        for (int32 Attempt = 0; Attempt < 20; ++Attempt)
        {
            Move(0, Head()); Move(1, Belt->GetHandleWorldLocation());
            bPassed &= !Hands[0]->TryGrabRagdoll() && !Hands[1]->TryGrabRagdoll();
            Belt->GetBeltComponent()->OnHandleGrabbed(Hands[1], TEXT("BeltHandle_Front"), Belt->GetHandleWorldLocation());
        }
        Manager->ResetTransfer(); Transition->OnSettleCancelled.Broadcast();
        Check(Patient->GetInteractionPhase() == EPatientInteractionPhase::Complete && HandlesClear()
            && !Patient->IsKinematicCarryActive() && !Patient->GetPatientMesh()->IsSimulatingPhysics(), TEXT("Completion resists repeated grips, late failure callbacks and score reset"));
        Patient->SetPatientState(EPatientState::LyingDown);
        Check(Patient->GetInteractionPhase() == EPatientInteractionPhase::BedPreparation, TEXT("Explicit attempt reset re-arms preparation"));
        Patient->GetPatientCinematicComponent()->bEnabled = false;
        Patient->SetPatientState(EPatientState::Seated);
        Check(Patient->GetInteractionPhase() == EPatientInteractionPhase::BeltTransfer
            && !Patient->GetPatientCinematicComponent()->IsCinematicActive(), TEXT("Disabled cinematic enables belt after bed animation"));
        Finish();
    }
};
static FAutoConsoleCommandWithWorld PatientInteractionSmoke(TEXT("cna.Patient.SmokeInteractionLocks"),
    TEXT("Test actual head/belt grabs, fade lock, carry cancellation, chair completion and disabled room UI; exits with test status."),
    FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
    {
        if (!World || !World->IsGameWorld()) return;
        TSharedRef<FPatientInteractionSmoke> Test = MakeShared<FPatientInteractionSmoke>(); Test->World = World;
        FTimerHandle Start; World->GetTimerManager().SetTimer(Start, [Test]() { Test->Start(); }, 1.f, false);
    }));
}
#endif
