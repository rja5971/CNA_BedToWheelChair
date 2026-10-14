#include "TransferManagerActor.h"
#include "../Reliability/CNAReliabilityLibrary.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UnrealType.h"
#include "UObject/StructOnScope.h"
#include "BeltActor.h"
#include "WheelchairActor.h"
#include "../Patient/PatientActor.h"
#include "../Components/GrabComponent.h"
#include "../Components/BeltComponent.h"
#include "../Components/PatientCarryComponent.h"
#include "../Components/PatientCinematicComponent.h"
#include "../Components/SeatedTransitionComponent.h"
#include "../Components/ScoringComponent.h"
#include "../StateMachine/TransferStateMachine.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/TimelineComponent.h"
#include "Curves/CurveFloat.h"
#include "Components/WidgetComponent.h"
#include "Components/Border.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "PhysicsEngine/BodyInstance.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "HAL/IConsoleManager.h"

#if !UE_BUILD_SHIPPING
namespace
{
struct FGuidedTransferSmoke : TSharedFromThis<FGuidedTransferSmoke>
{
    UWorld* World;
    ATransferManagerActor* Manager = nullptr;
    APatientActor* Patient = nullptr;
    ABeltActor* Belt = nullptr;
    AWheelchairActor* Chair = nullptr;
    UGrabComponent* Hands[2] = {};
    USceneComponent* Origins[2] = {};
    FTransform Checkpoint;
    FTimerHandle Timer;
    float StageStarted = 0;
    int32 Stage = 0, Retries = 0;
    bool bPassed = true;
    float RetryScore = 0;
    UAnimSequence* ChairAnimation = nullptr;
    explicit FGuidedTransferSmoke(UWorld* InWorld) : World(InWorld) {}
    void Check(bool Value, const TCHAR* Label)
    {
        bPassed &= Value;
        UE_LOG(LogTemp, Display, TEXT("GUIDED_TRANSFER %s: %s"), Label, Value ? TEXT("PASS") : TEXT("FAIL"));
    }
    void Next(int32 Value) { Stage = Value; StageStarted = World->GetTimeSeconds(); }
    void Finish()
    {
        World->GetTimerManager().ClearTimer(Timer);
        UE_LOG(LogTemp, Display, TEXT("GUIDED_TRANSFER RESULT: %s"), bPassed ? TEXT("PASS") : TEXT("FAIL"));
        FPlatformMisc::RequestExitWithStatus(false, bPassed ? 0 : 1);
    }
    void Move(int32 Index, FVector Location) { Origins[Index]->SetWorldLocation(Location); }
    bool Grab(int32 Index) { Move(Index, Belt->GetHandleWorldLocation()); return Hands[Index]->TryGrabRagdoll() && Hands[Index]->GetGrabbedActor() == Belt; }
    void Start()
    {
        for (TActorIterator<ATransferManagerActor> It(World); It; ++It) if (It->bGuidedTraining) Manager = *It;
        for (TActorIterator<APatientActor> It(World); It; ++It) Patient = *It;
        for (TActorIterator<ABeltActor> It(World); It; ++It) if (It->GetInteractionPatient() == Patient) Belt = *It;
        for (TActorIterator<AWheelchairActor> It(World); It; ++It) if (It->IsReadyToReceive()) { Chair = *It; break; }
        Check(Manager && Patient && Belt && Chair, TEXT("Real training actors resolved"));
        if (!bPassed) { Finish(); return; }
        Check(Manager->GetTrainingStage() == EGuidedTrainingStage::Transfer, TEXT("Real guided quiz authorized transfer"));
        for (int32 Index = 0; Index < 2; ++Index)
        {
            AActor* Actor = World->SpawnActor<AActor>();
            Origins[Index] = NewObject<USceneComponent>(Actor); Actor->SetRootComponent(Origins[Index]);
            Actor->AddInstanceComponent(Origins[Index]); Origins[Index]->RegisterComponent();
            Hands[Index] = NewObject<UGrabComponent>(Actor); Actor->AddInstanceComponent(Hands[Index]);
            Hands[Index]->SetTraceOrigin(Origins[Index]); Hands[Index]->RegisterComponent();
        }
        Check(!Grab(0), TEXT("Premature belt grip denied"));
        FBodyInstance* Body = Patient->GetPatientMesh()->GetBodyInstance(TEXT("head"));
        Move(0, Body ? Body->GetCOMPosition() : Patient->GetPatientMesh()->GetBoneLocation(TEXT("head")));
        Check(Hands[0]->TryGrabRagdoll() && Hands[0]->GetGrabbedActor() == Patient, TEXT("Native head support acquired"));
        Patient->GetPatientCinematicComponent()->bUseMeshFadeWithoutHMD = true;
        Patient->SetPatientState(EPatientState::Seated);
        Check(!Hands[0]->IsGrabbing() && Manager->IsTrainingMovementAllowed(), TEXT("Bed settle cancels grab while allowing locomotion"));
        Next(1);
        TSharedRef<FGuidedTransferSmoke> Self = AsShared();
        World->GetTimerManager().SetTimer(Timer, [Self]() { Self->Tick(); }, .05f, true);
    }
    void Tick()
    {
        TSharedRef<FGuidedTransferSmoke> KeepAlive = AsShared();
        const float Age = World->GetTimeSeconds() - StageStarted;
        if (Age > 25.f) { Check(false, TEXT("Stage deadline")); Finish(); return; }
        if (!bPassed) { Finish(); return; }
        if (Stage == 1)
        {
            if (Patient->GetInteractionPhase() != EPatientInteractionPhase::BeltTransfer)
            {
                Move(0, Belt->GetActorLocation()); bPassed &= !Hands[0]->TryGrabRagdoll(); return;
            }
            if (!Belt->IsGrabInteractionEnabled()) return;
            Check(Patient->HasBedCheckpoint(), TEXT("Post-rotation seated checkpoint captured"));
            Check(!Patient->IsGrabInteractionEnabled() && Manager->IsTrainingMovementAllowed(), TEXT("Belt-only phase restores locomotion"));
            Checkpoint = Patient->GetPatientMesh()->GetComponentTransform();
            Check(Belt->GetBeltComponent()->AttachToPatient(Patient), TEXT("Native belt attachment succeeds after sitting"));
            Next(2);
        }
        else if (Stage == 2 && Age > .2f)
        {
            Check(Grab(0) && Grab(1), TEXT("Two native belt handles acquired"));
            Hands[0]->ReleaseRagdoll();
            Check(Belt->GetBeltComponent()->GetActiveGrabCount() == 1 && Patient->IsKinematicCarryActive(), TEXT("One-hand release preserves carry"));
            Check(Grab(0), TEXT("Released hand reacquires"));
            Move(0, Origins[0]->GetComponentLocation() + FVector(0, 0, 50));
            Move(1, Origins[1]->GetComponentLocation() + FVector(0, 0, 50));
            Next(3);
        }
        else if (Stage == 3 && Age > .15f)
        {
            Hands[0]->ReleaseRagdoll(); Hands[1]->ReleaseRagdoll(); Next(4);
        }
        else if (Stage == 4 && Age > .1f)
        {
            Check(Grab(0), TEXT("Re-grab within grace period accepted")); Next(5);
        }
        else if (Stage == 5 && Age > .5f)
        {
            Check(Hands[0]->IsGrabbing() && Patient->IsKinematicCarryActive(), TEXT("Re-grab cancels delayed bed retry"));
            Hands[0]->ReleaseRagdoll(); RetryScore = Manager->GetScoring()->GetCurrentScore(); Next(6);
        }
        else if (Stage == 6 && Age > .65f)
        {
            Check(Patient->GetPatientMesh()->GetComponentTransform().Equals(Checkpoint, .1f), TEXT("Interrupted lift returns to actual bed checkpoint"));
            Check(!Patient->IsKinematicCarryActive() && !Patient->GetPatientMesh()->IsSimulatingPhysics(), TEXT("Retry retains seated animation without delayed physics wake"));
            Check(Patient->HasBeltAttached() && Belt->IsGrabInteractionEnabled() && !Patient->IsGrabInteractionEnabled(), TEXT("Retry retains belt and correct permissions"));
            Check(Manager->GetStateMachine()->GetCurrentState() == ETransferState::BeltLift, TEXT("Retry resumes only the lift step"));
            Check(FMath::IsNearlyEqual(Manager->GetScoring()->GetCurrentScore(), RetryScore, .05f), TEXT("Retry awards no duplicate step score"));
            ++Retries;
            if (Retries < 3)
            {
                Check(Grab(0), TEXT("Repeat retry re-grab succeeds")); Hands[0]->ReleaseRagdoll(); Next(6);
            }
            else
            {
                Check(Grab(0), TEXT("Carry resumes for wheelchair approach"));
                Manager->GetStateMachine()->ForceState(ETransferState::WheelchairTransfer);
                Chair->LockBrakes();
                Patient->GetPatientMesh()->AddWorldOffset(Chair->GetTargetSeatTransform().GetLocation() - Patient->GetPelvisLocation(), false, nullptr, ETeleportType::TeleportPhysics);
                Patient->GetPatientMesh()->RefreshBoneTransforms();
                ChairAnimation = Patient->GetSeatedTransitionComponent()->SeatedAnimation;
                Patient->GetSeatedTransitionComponent()->SeatedAnimation = nullptr;
                Hands[0]->ReleaseRagdoll(); Next(9);
            }
        }
        else if (Stage == 9 && Age > .15f)
        {
            Patient->GetSeatedTransitionComponent()->SeatedAnimation = ChairAnimation;
            Check(Manager->GetTrainingStage() == EGuidedTrainingStage::Transfer && !Chair->IsOccupied(), TEXT("Rejected seating animation cannot complete procedure"));
            Next(10);
        }
        else if (Stage == 10 && Age > .65f)
        {
            Check(Patient->GetPatientMesh()->GetComponentTransform().Equals(Checkpoint, .1f) && Belt->IsGrabInteractionEnabled(), TEXT("Rejected chair handoff safely returns to bed for retry"));
            Check(Grab(0), TEXT("Retry after failed seating reacquires belt"));
            Manager->GetStateMachine()->ForceState(ETransferState::WheelchairTransfer);
            Patient->GetPatientMesh()->AddWorldOffset(Chair->GetTargetSeatTransform().GetLocation() - Patient->GetPelvisLocation(), false, nullptr, ETeleportType::TeleportPhysics);
            Patient->GetPatientMesh()->RefreshBoneTransforms();
            Hands[0]->ReleaseRagdoll(); Next(7);
        }
        else if (Stage == 7 && Manager->GetTrainingStage() == EGuidedTrainingStage::Complete)
        {
            USceneComponent* Parent = Patient->GetPatientMesh()->GetAttachParent();
            AWheelchairActor* Selected = Parent ? Cast<AWheelchairActor>(Parent->GetOwner()) : nullptr;
            Check(Patient->GetInteractionPhase() == EPatientInteractionPhase::Complete && Selected && Selected->IsOccupied()
                && Parent == Selected->GetSeatTargetComponent(), TEXT("Valid selected-chair release completes exactly once"));
            if (Selected) Chair = Selected;
            const float Score = Manager->GetScoring()->GetCurrentScore();
            Manager->GetStateMachine()->ForceState(ETransferState::Complete);
            Check(Manager->GetScoring()->GetCurrentScore() == Score, TEXT("Repeated completion cannot repeat reward"));
            Check(!Grab(0), TEXT("Completed belt denies further grabs"));
            Move(0, Patient->GetPatientMesh()->GetBoneLocation(TEXT("head")));
            Check(!Hands[0]->TryGrabRagdoll(), TEXT("Completed patient denies further grabs"));
            TArray<UWidgetComponent*> Panels; Manager->GetComponents(Panels);
            UBorder* ResultBorder = nullptr;
            UWidgetComponent* ResultPanel = nullptr;
            for (UWidgetComponent* Panel : Panels)
                if (Panel->GetFName() == TEXT("TrainingStatus"))
                {
                    ResultPanel = Panel;
                    if (UUserWidget* Widget = Panel->GetUserWidgetObject())
                        if (Widget->WidgetTree) ResultBorder = Cast<UBorder>(Widget->WidgetTree->FindWidget(TEXT("Border_102")));
                }
            Check(ResultBorder && ResultBorder->GetPadding().Bottom >= ResultBorder->GetPadding().Top + Manager->ResultPopupExtraBottomPadding,
                TEXT("Final native result panel reserves additional bottom padding"));
            Check(ResultPanel && ResultPanel->GetDrawSize().Y >= 600,
                TEXT("Final result surface grows to fit long scores and bottom padding"));
            Next(8);
        }
        else if (Stage == 8 && Age > 1.f)
        {
            Check(Patient->GetPatientMesh()->GetAttachParent() == Chair->GetSeatTargetComponent()
                && !Patient->GetPatientMesh()->IsSimulatingPhysics(), TEXT("Late carry recovery cannot undo final chair seating"));
            Finish();
        }
    }
};
static FAutoConsoleCommandWithWorld GuidedMovementSmoke(TEXT("cna.Guided.TestMovement"),
    TEXT("Verify actual pending teleports are accepted inside the training radius and rejected outside it."),
    FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
    {
        APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0);
        if (!Pawn) return;
        const FVector Before = Pawn->GetActorLocation();
        const FRotator BeforeRotation = Pawn->GetActorRotation();
        const FVector View = UCNAReliabilityLibrary::TrainingViewLocation(Pawn);
        ATransferManagerActor* Manager = UCNAReliabilityLibrary::GuidedManager(Pawn);
        FVector Center;
        if (!Manager || !Manager->IsLessonBoundaryActive() || !Manager->IsTrainingMovementAllowed() || !Manager->GetLessonMovementCenter(Center))
        { UE_LOG(LogTemp, Error, TEXT("GUIDED_MOVEMENT RESULT: FAIL (lesson boundary is not ready)")); return; }
        FVector Front;
        const FVector Outward = (View - Center).GetSafeNormal2D();
        const bool bAtEdge = FMath::Abs(FVector::Dist2D(View, Center) - Manager->TrainingMovementRadiusMeters * 100.f) < .1f;
        const USceneComponent* Camera = Cast<USceneComponent>(UCNAReliabilityLibrary::ObjectProperty(Pawn, TEXT("Camera")));
        const bool bFrontFacing = Manager->GetLessonMovementFront(Front) && FVector::DotProduct(Outward, Front) > 0.f
            && Camera && FVector::DotProduct(Camera->GetForwardVector().GetSafeNormal2D(), -Outward) > .99f;
        const bool bBackwardBlocked = Manager->ClampTrainingMovementDelta(Pawn, Outward * 20.f).Size() < .02f;
        const bool bForwardAllowed = Manager->ClampTrainingMovementDelta(Pawn, -Outward * 20.f).Size() > 19.9f;
        FBoolProperty* Valid = FindFProperty<FBoolProperty>(Pawn->GetClass(), TEXT("bValidTeleportLocation"));
        FStructProperty* Target = FindFProperty<FStructProperty>(Pawn->GetClass(), TEXT("ProjectedTeleportLocation"));
        if (!Valid || !Target) { UE_LOG(LogTemp, Error, TEXT("GUIDED_MOVEMENT RESULT: FAIL (missing verified properties)")); return; }
        FVector* Location = Target->ContainerPtrToValuePtr<FVector>(Pawn);
        // Verify the current tracked view is inside the panel-centred circle.
        const bool bInside = UCNAReliabilityLibrary::ValidateTrainingTeleportLocation(Pawn, true, View);
        *Location = Center + FVector(Manager->TrainingMovementRadiusMeters * 100.f + 100.f, 0, 0); Valid->SetPropertyValue_InContainer(Pawn, true);
        UCNAReliabilityLibrary::CallNoArgs(Pawn, TEXT("TryTeleport"));
        const bool bOutsideRejected = Pawn->GetActorLocation().Equals(Before, .01f);
        *Location = Center + (View - Center).GetSafeNormal2D() * (Manager->TrainingMovementRadiusMeters * 50.f); Location->Z = Before.Z;
        Valid->SetPropertyValue_InContainer(Pawn, true);
        UCNAReliabilityLibrary::CallNoArgs(Pawn, TEXT("TryTeleport"));
        const bool bInsideAccepted = !Pawn->GetActorLocation().Equals(Before, .01f);
        Pawn->SetActorLocation(Before, false, nullptr, ETeleportType::TeleportPhysics);
        const FRotator Rotation = Pawn->GetActorRotation();
        if (UFunction* Turn = Pawn->FindFunction(TEXT("SnapTurn")))
        {
            FStructOnScope Params(Turn);
            if (FBoolProperty* Right = FindFProperty<FBoolProperty>(Turn, TEXT("bRightTurn")))
                Right->SetPropertyValue_InContainer(Params.GetStructMemory(), true);
            Pawn->ProcessEvent(Turn, Params.GetStructMemory());
        }
        const bool bTurnAccepted = !Pawn->GetActorRotation().Equals(Rotation, .01f);
        Pawn->SetActorRotation(Rotation, ETeleportType::TeleportPhysics);
        Pawn->SetActorLocation(Before, false, nullptr, ETeleportType::TeleportPhysics);
        Valid->SetPropertyValue_InContainer(Pawn, false);
        Pawn->SetActorLocation(Before + FVector(Manager->TrainingMovementRadiusMeters * 100.f + 1000.f, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
        const bool bArrivalCorrected = Manager->CorrectLessonArrival() && Manager->IsTrainingLocationAllowed(UCNAReliabilityLibrary::TrainingViewLocation(Pawn));
        Pawn->SetActorRotation(BeforeRotation, ETeleportType::TeleportPhysics);
        Pawn->SetActorLocation(Before, false, nullptr, ETeleportType::TeleportPhysics);
        UE_LOG(LogTemp, Display, TEXT("GUIDED_MOVEMENT RESULT: %s (inside=%d outside=%d teleport=%d turn=%d arrival=%d edge=%d front=%d backwardBlocked=%d forwardAllowed=%d)"),
            bInside && bOutsideRejected && bInsideAccepted && bTurnAccepted && bArrivalCorrected
                && bAtEdge && bFrontFacing && bBackwardBlocked && bForwardAllowed ? TEXT("PASS") : TEXT("FAIL"),
            bInside, bOutsideRejected, bInsideAccepted, bTurnAccepted, bArrivalCorrected,
            bAtEdge, bFrontFacing, bBackwardBlocked, bForwardAllowed);
    }));
static FAutoConsoleCommandWithWorldAndArgs GuidedArrivalStage(TEXT("cna.Guided.TestArrivalStage"),
    TEXT("Development-only focused arrival check: stages 2, 6, 7."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        if (Args.Num() != 1) return;
        ATransferManagerActor* Manager = UCNAReliabilityLibrary::GuidedManager(UGameplayStatics::GetPlayerPawn(World, 0));
        if (!Manager) return;
        const int32 Stage = FCString::Atoi(*Args[0]);
        if (Stage != 2 && Stage != 6 && Stage != 7) return;
        if (Stage == 7)
        {
            AActor* Quiz = nullptr;
            AActor* Video = nullptr;
            for (TActorIterator<AActor> It(World); It; ++It)
            {
                if (It->GetClass()->GetFName() == TEXT("BP_QuizActor_C")) Quiz = *It;
                if (It->GetFName() == TEXT("BP_MediaDisplay_C_0")) Video = *It;
            }
            if (!Quiz || !Video) return;
            Quiz->SetActorLocationAndRotation(Video->GetActorLocation(), Video->GetActorQuat());
        }
        Manager->SetTrainingStage(static_cast<EGuidedTrainingStage>(Stage));
    }));
static FAutoConsoleCommandWithWorld GuidedFadeInspect(TEXT("cna.Guided.InspectFade"),
    TEXT("Inspect the actual pawn fade timeline and opacity curve."),
    FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
    {
        APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0);
        UTimelineComponent* Timeline = Pawn ? Cast<UTimelineComponent>(UCNAReliabilityLibrary::ObjectProperty(Pawn, TEXT("FadeTimeline"))) : nullptr;
        if (!Timeline) return;
        UE_LOG(LogTemp, Display, TEXT("FADE_CURVE: length=%f looping=%d"), Timeline->GetTimelineLength(), Timeline->IsLooping());
        TSet<UCurveBase*> Curves; Timeline->GetAllCurves(Curves);
        for (UCurveBase* Base : Curves)
            if (UCurveFloat* Curve = Cast<UCurveFloat>(Base))
                for (const FRichCurveKey& Key : Curve->FloatCurve.GetConstRefOfKeys())
                    UE_LOG(LogTemp, Display, TEXT("FADE_CURVE: %s time=%f opacity=%f"), *Curve->GetName(), Key.Time, Key.Value);
    }));
static FAutoConsoleCommandWithWorld GuidedTransferSmoke(TEXT("cna.Guided.SmokeTransfer"),
    TEXT("Validate the real guided transfer after the lesson quiz, including repeated bed retries and completion."),
    FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
    {
        if (!World || !World->IsGameWorld()) return;
        MakeShared<FGuidedTransferSmoke>(World)->Start();
    }));
}
#endif
