#include "TransferManagerActor.h"
#include "WheelchairActor.h"
#include "../Reliability/CNAReliabilityLibrary.h"
#include "../Reliability/TrainingMediaControllerComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "EngineUtils.h"
#include "TimerManager.h"

namespace
{
bool LessonFloor(UWorld* World, const FVector& Point, const FCollisionQueryParams& Query, FHitResult& Floor)
{
    TArray<FHitResult> Hits;
    FCollisionQueryParams FloorQuery(Query);
    FloorQuery.bTraceComplex = true;
    World->LineTraceMultiByObjectType(Hits, Point + FVector(0, 0, 50), Point - FVector(0, 0, 500),
        FCollisionObjectQueryParams(ECC_WorldStatic), FloorQuery);
    bool bFound = false;
    for (const FHitResult& Hit : Hits)
        if (Hit.bBlockingHit && Hit.ImpactNormal.Z >= .7f && (!bFound || Hit.ImpactPoint.Z < Floor.ImpactPoint.Z))
        { Floor = Hit; bFound = true; }
    if (bFound)
    {
        const float BaseFloorZ = Floor.ImpactPoint.Z;
        // Finish flooring can sit above the structural slab. Use its top, without moving
        // the area onto a bed/table above the floor.
        for (const FHitResult& Hit : Hits)
            if (Hit.bBlockingHit && Hit.ImpactNormal.Z >= .7f && Hit.ImpactPoint.Z <= BaseFloorZ + 30.f
                && Hit.ImpactPoint.Z > Floor.ImpactPoint.Z) Floor = Hit;
    }
    return bFound;
}
}

bool ATransferManagerActor::GetLessonMovementCenter(FVector& Center) const
{
    const bool bQuiz = TrainingStage == EGuidedTrainingStage::EntryQuiz || TrainingStage == EGuidedTrainingStage::PatientQuiz;
    const bool bBathroom = TrainingStage == EGuidedTrainingStage::BathroomVideo || TrainingStage == EGuidedTrainingStage::BathroomTransition;
    AActor* AnchorActor = bQuiz ? QuizActor.Get() : bBathroom ? FindLesson(1)
        : TrainingStage == EGuidedTrainingStage::TransferVideo ? FindLesson(2) : nullptr;
    const USceneComponent* Anchor = Cast<USceneComponent>(UCNAReliabilityLibrary::ObjectProperty(AnchorActor,
        bQuiz ? TEXT("QuizPanel") : TEXT("ScreenMesh")));
    if (!IsValid(Anchor)) return false;
    Center = Anchor->GetComponentLocation();
    return !Center.ContainsNaN();
}

bool ATransferManagerActor::IsLessonBoundaryActive() const
{
    if (!bGuidedTraining || bLessonRelocating || bWrongAnswerOpen) return false;
    if (TrainingStage == EGuidedTrainingStage::EntryQuiz || TrainingStage == EGuidedTrainingStage::PatientQuiz) return true;
    if (TrainingStage != EGuidedTrainingStage::BathroomVideo && TrainingStage != EGuidedTrainingStage::TransferVideo) return false;
    if (!LessonMediaController.IsValid())
        for (TActorIterator<AActor> It(GetWorld()); It; ++It)
            if (UTrainingMediaControllerComponent* Controller = It->FindComponentByClass<UTrainingMediaControllerComponent>())
            { LessonMediaController = Controller; break; }
    // Loading/recovery text and Retry are popups, so only active playback has a boundary.
    return LessonMediaController.IsValid() && LessonMediaController->GetPlaybackState() == FTrainingPlaybackSession::EState::Playing;
}

bool ATransferManagerActor::GetLessonMovementFront(FVector& Front) const
{
    const bool bQuiz = TrainingStage == EGuidedTrainingStage::EntryQuiz || TrainingStage == EGuidedTrainingStage::PatientQuiz;
    const bool bBathroom = TrainingStage == EGuidedTrainingStage::BathroomVideo || TrainingStage == EGuidedTrainingStage::BathroomTransition;
    AActor* Actor = bQuiz ? QuizActor.Get() : bBathroom ? FindLesson(1)
        : TrainingStage == EGuidedTrainingStage::TransferVideo ? FindLesson(2) : nullptr;
    // World widgets face along local +X. A video's caption/Retry widget shares its readable side.
    const USceneComponent* Facing = Cast<USceneComponent>(UCNAReliabilityLibrary::ObjectProperty(Actor,
        bQuiz ? TEXT("QuizPanel") : TEXT("TextWidget")));
    if (!Facing) return false;
    Front = Facing->GetForwardVector().GetSafeNormal2D();
    return !Front.IsNearlyZero();
}

bool ATransferManagerActor::CorrectLessonArrival()
{
    if (!bGuidedTraining) return true;
    FVector Center;
    if (!GetLessonMovementCenter(Center)) return true; // Conversations and physical transfer have no lesson circle.
    APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
    if (!Pawn) return false;
    const FVector View = UCNAReliabilityLibrary::TrainingViewLocation(Pawn);
    const FVector OriginalActorLocation = Pawn->GetActorLocation();
    const FRotator OriginalActorRotation = Pawn->GetActorRotation();
    const USceneComponent* ArrivalCamera = Cast<USceneComponent>(UCNAReliabilityLibrary::ObjectProperty(Pawn, TEXT("Camera")));
    const float OriginalViewYaw = ArrivalCamera ? ArrivalCamera->GetComponentRotation().Yaw : OriginalActorRotation.Yaw;
    const UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(Pawn->GetRootComponent());
    const float StandingHalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 88.f;
    const float StandingRadius = Capsule ? Capsule->GetScaledCapsuleRadius() : 20.f;
    const float Radius = FMath::Max(.1f, TrainingMovementRadiusMeters) * 100.f;
    if (!bLessonArrivalPending && FVector::DistSquared2D(View, Center) <= FMath::Square(Radius)) return true;
    FVector Front;
    if (!GetLessonMovementFront(Front))
    {
        TransferFailed(FText::FromString(TEXT("Cannot determine the lesson's front side. Check the quiz/video widget orientation in the editor.")));
        return false;
    }
    UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    FVector Direction = Front;
    const bool bPatientQuiz = TrainingStage == EGuidedTrainingStage::PatientQuiz;
    const bool bAssignedTarget = bPatientQuiz && IsValid(PatientQuizArrivalTarget);
    if (bAssignedTarget)
    {
        Direction = (PatientQuizArrivalTarget->GetActorLocation() - Center).GetSafeNormal2D();
        if (FVector::DotProduct(Direction, Front) <= 0.f)
        {
            TransferFailed(FText::FromString(TEXT("Patient Quiz Arrival Target must choose a direction in front of the quiz. Its direction is projected onto the lesson circle's edge.")));
            return false;
        }
    }
    else if (bPatientQuiz)
    {
        Direction = Front;
        const FVector Right = FVector::CrossProduct(FVector::UpVector, Front);
        AWheelchairActor* Nearest = nullptr;
        float Closest = TNumericLimits<float>::Max();
        for (TActorIterator<AWheelchairActor> It(GetWorld()); It; ++It)
        {
            const float Distance = FVector::DistSquared2D(It->GetTargetSeatTransform().GetLocation(), Center);
            if (It->IsReadyToReceive() && Distance < Closest) { Closest = Distance; Nearest = *It; }
        }
        if (Nearest)
        {
            const FVector ToChair = (Nearest->GetTargetSeatTransform().GetLocation() - Center).GetSafeNormal2D();
            Direction = (Front + Right * FVector::DotProduct(ToChair, Right) * .6f).GetSafeNormal2D();
        }
    }
    FCollisionQueryParams Query(SCENE_QUERY_STAT(LessonArrival), true);
    Query.AddIgnoredActor(this); Query.AddIgnoredActor(Pawn); Query.AddIgnoredActor(QuizActor);
    Query.AddIgnoredActor(FindLesson(TrainingStage == EGuidedTrainingStage::TransferVideo ? 2 : 1));
    FCollisionQueryParams ClearanceQuery = Query;
    ClearanceQuery.bTraceComplex = false;
    int32 FloorCandidates = 0, BlockedCandidates = 0;
    // Stay on the circumference: try the preferred front point, then nearby front-side angles
    // if furniture/walls block it. The tiny inset only avoids floating-point boundary rejection.
    const float ArrivalRadius = Radius - .01f;
    for (int32 Angle = 0; Angle < (bAssignedTarget ? 1 : 24); ++Angle)
    {
        const int32 FrontAngle = (Angle + 1) / 2 * (Angle % 2 ? 1 : -1);
        const FVector Candidate = Center + Direction.RotateAngleAxis(FrontAngle * 10.f, FVector::UpVector) * ArrivalRadius;
        if (FVector::DotProduct(Candidate - Center, Front) <= FMath::Min(20.f, Radius * .1f)) continue;
        FNavLocation Projected;
        FHitResult Floor;
        if (LessonFloor(GetWorld(), Candidate, Query, Floor))
        {
            // The delivery map has no saved navigation actor in the entry area. Validate actual floor
            // collision and standing clearance instead of treating a guessed location as walkable.
            ++FloorCandidates;
            Projected.Location = FVector(Candidate.X, Candidate.Y, Floor.ImpactPoint.Z + 2.f);
        }
        else if (!Navigation || !Navigation->ProjectPointToNavigation(Candidate, Projected, FVector(1.f, 1.f, 300.f))
            || FVector::DistSquared2D(Projected.Location, Candidate) > .01f) continue;
        if (GetWorld()->OverlapBlockingTestByChannel(Projected.Location + FVector(0, 0, StandingHalfHeight), FQuat::Identity,
            ECC_Pawn, FCollisionShape::MakeCapsule(StandingRadius, StandingHalfHeight), ClearanceQuery)) { ++BlockedCandidates; continue; }
        FRotator Rotation = OriginalActorRotation;
        {
            const float DesiredViewYaw = (Center - Projected.Location).Rotation().Yaw;
            Rotation.Yaw = OriginalActorRotation.Yaw + FMath::FindDeltaAngleDegrees(OriginalViewYaw, DesiredViewYaw);
        }
        const FVector ViewOffset = FVector(View.X - OriginalActorLocation.X, View.Y - OriginalActorLocation.Y, 0.f)
            .RotateAngleAxis(Rotation.Yaw - OriginalActorRotation.Yaw, FVector::UpVector);
        const FVector Destination = Projected.Location - ViewOffset + FVector(0.f, 0.f, Capsule ? StandingHalfHeight : 0.f);
        // Floor and standing clearance were checked above; prevent engine adjustment off the circle.
        if (Pawn->TeleportTo(Destination, Rotation, false, true))
        {
            // Account for the pawn's teleport callback updating its camera/origin transform.
            const FVector ActualView = UCNAReliabilityLibrary::TrainingViewLocation(Pawn);
            Pawn->AddActorWorldOffset(FVector(Projected.Location.X - ActualView.X, Projected.Location.Y - ActualView.Y, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
            const USceneComponent* Camera = Cast<USceneComponent>(UCNAReliabilityLibrary::ObjectProperty(Pawn, TEXT("Camera")));
            if (Camera)
            {
                const float YawCorrection = FMath::FindDeltaAngleDegrees(Camera->GetComponentRotation().Yaw, (Center - Projected.Location).Rotation().Yaw);
                const FVector BeforeTurnView = UCNAReliabilityLibrary::TrainingViewLocation(Pawn);
                Pawn->AddActorWorldRotation(FRotator(0.f, YawCorrection, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
                const FVector AfterTurnView = UCNAReliabilityLibrary::TrainingViewLocation(Pawn);
                Pawn->AddActorWorldOffset(FVector(BeforeTurnView.X - AfterTurnView.X, BeforeTurnView.Y - AfterTurnView.Y, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
            }
            // Keep controller-driven pawn yaw from restoring its pre-teleport direction next tick.
            if (AController* Controller = Pawn->GetController())
            {
                FRotator ControlRotation = Controller->GetControlRotation();
                ControlRotation.Yaw = Pawn->GetActorRotation().Yaw;
                Controller->SetControlRotation(ControlRotation);
            }
            bLessonArrivalPending = false;
            UE_LOG(LogTemp, Display, TEXT("LESSON_ARRIVAL: corrected destination into %.2fm circle centred on %s"), TrainingMovementRadiusMeters, *Center.ToString());
            UE_LOG(LogTemp, Display, TEXT("LESSON_EDGE_ARRIVAL: stage=%d distance=%.3fcm front=%.3f facing inward position=%s"),
                int32(TrainingStage), FVector::Dist2D(UCNAReliabilityLibrary::TrainingViewLocation(Pawn), Center),
                FVector::DotProduct((Projected.Location - Center).GetSafeNormal2D(), Front), *Projected.Location.ToString());
            if (bPatientQuiz) UE_LOG(LogTemp, Display, TEXT("PATIENT_QUIZ_ARRIVAL: front-side placement, target=%s, position=%s"),
                bAssignedTarget ? *PatientQuizArrivalTarget->GetName() : TEXT("automatic chair side"), *Projected.Location.ToString());
            return true;
        }
    }
    UE_LOG(LogTemp, Warning, TEXT("LESSON_ARRIVAL: no destination; center=%s view=%s floor candidates=%d blocked=%d"),
        *Center.ToString(), *View.ToString(), FloorCandidates, BlockedCandidates);
    TransferFailed(FText::FromString(TEXT("No clear floor position fits on this lesson circle's front edge. Adjust Lesson Movement Radius, lesson placement, or the arrival target in the editor.")));
    return false;
}

void ATransferManagerActor::PrepareLessonArrival()
{
    if (!bGuidedTraining || bLessonRelocating || bWrongAnswerOpen || !GetWorld()->IsGameWorld()) return;
    if (TrainingStage != EGuidedTrainingStage::EntryQuiz && TrainingStage != EGuidedTrainingStage::PatientQuiz
        && TrainingStage != EGuidedTrainingStage::BathroomVideo && TrainingStage != EGuidedTrainingStage::TransferVideo) return;
    FVector Center;
    APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
    if (!Pawn || !GetLessonMovementCenter(Center)) return;
    if (!bLessonArrivalPending
        && FVector::DistSquared2D(UCNAReliabilityLibrary::TrainingViewLocation(Pawn), Center)
        <= FMath::Square(FMath::Max(.1f, TrainingMovementRadiusMeters) * 100.f)) return;
    bLessonRelocating = true;
    UCNAReliabilityLibrary::TrainingFadeOut(this);
    const EGuidedTrainingStage Expected = TrainingStage;
    GetWorldTimerManager().SetTimer(LessonArrivalTimer, [this, Expected]()
    {
        const bool bAccepted = TrainingStage == Expected && CorrectLessonArrival();
        UCNAReliabilityLibrary::TrainingFadeIn(this);
        // Preserve the half-second fade-in lock, including pending teleport commit.
        GetWorldTimerManager().SetTimer(LessonArrivalTimer, [this, Expected, bAccepted]()
        {
            bLessonRelocating = false;
            UpdateTrainingUI();
            if (bAccepted && TrainingStage == Expected)
            {
                if (Expected == EGuidedTrainingStage::BathroomVideo) UCNAReliabilityLibrary::RequestPlayback(FindLesson(1));
                if (Expected == EGuidedTrainingStage::TransferVideo) UCNAReliabilityLibrary::RequestPlayback(FindLesson(2));
            }
        }, .5f, false);
    }, 1.f, false);
}

void ATransferManagerActor::UpdateWalkHologram()
{
    FVector Center;
    const bool bVisible = bShowAllowedWalkHologram && IsLessonBoundaryActive() && IsTrainingMovementAllowed() && GetLessonMovementCenter(Center);
    if (!bVisible) { if (WalkHologram) WalkHologram->SetVisibility(false); return; }
    FCollisionQueryParams Query(SCENE_QUERY_STAT(LessonFloor), false);
    Query.AddIgnoredActor(this);
    Query.AddIgnoredActor(UGameplayStatics::GetPlayerPawn(this, 0));
    Query.AddIgnoredActor(QuizActor);
    Query.AddIgnoredActor(FindLesson(TrainingStage == EGuidedTrainingStage::BathroomVideo ? 1 : 2));
    FHitResult Floor;
    if (!LessonFloor(GetWorld(), Center, Query, Floor))
    { if (WalkHologram) WalkHologram->SetVisibility(false); return; }
    if (!WalkHologram)
    {
        if (!WalkHologramMaterial) return;
        WalkHologram = NewObject<UStaticMeshComponent>(this, TEXT("AllowedWalkHologram"));
        AddInstanceComponent(WalkHologram);
        WalkHologram->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")));
        WalkHologram->SetMaterial(0, WalkHologramMaterial);
        WalkHologram->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        WalkHologram->SetGenerateOverlapEvents(false);
        WalkHologram->SetCanEverAffectNavigation(false);
        WalkHologram->SetCastShadow(false);
        WalkHologram->RegisterComponent();
    }
    WalkHologram->SetWorldLocation(FVector(Center.X, Center.Y, Floor.ImpactPoint.Z + 2.f));
    WalkHologram->SetWorldRotation(FRotator::ZeroRotator);
    WalkHologram->SetWorldScale3D(FVector(FMath::Max(.1f, TrainingMovementRadiusMeters) * 2.f));
    WalkHologram->SetVisibility(true);
}
