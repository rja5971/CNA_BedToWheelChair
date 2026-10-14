#include "TransferManagerActor.h"
#include "BeltActor.h"
#include "../Patient/PatientActor.h"
#include "../StateMachine/TransferStateMachine.h"
#include "../Components/ScoringComponent.h"
#include "../Reliability/CNAReliabilityLibrary.h"
#include "../Reliability/TrainingMediaControllerComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Components/SceneComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/Border.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCNAGuidedTrainingTest, "CNA.GuidedTraining.GatesAndTerminalGuards", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCNAGuidedTrainingTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    ATransferManagerActor* Manager = World->SpawnActor<ATransferManagerActor>();
    APatientActor* Patient = World->SpawnActor<APatientActor>();
    ABeltActor* Belt = World->SpawnActor<ABeltActor>();
    Manager->PatientRef = Patient; Manager->BeltRef = Belt; Manager->bGuidedTraining = true;
    UWidgetComponent* Popup = NewObject<UWidgetComponent>(Manager); Manager->AddInstanceComponent(Popup); Popup->RegisterComponent();
    Popup->SetRelativeLocation(FVector(10, 20, 30));
    Manager->ApplyPopupOffset(Popup, FVector(5, 0, 15));
    Manager->ApplyPopupOffset(Popup, FVector(5, 0, 15));
    TestEqual(TEXT("Popup offset does not accumulate each tick"), Popup->GetRelativeLocation(), FVector(15, 20, 45));
    Manager->ApplyPopupOffset(Popup, FVector(-10, 30, 5));
    TestEqual(TEXT("Live editor changes are relative to original placement"), Popup->GetRelativeLocation(), FVector(0, 50, 35));
    UClass* PopupClass = LoadClass<AActor>(nullptr, TEXT("/Game/Project/Blueprints/Actors/BP_PopupActor.BP_PopupActor_C"));
    AActor* ContinueActor = PopupClass ? World->SpawnActor<AActor>(PopupClass) : nullptr;
    if (TestNotNull(TEXT("Actual Continue actor"), ContinueActor))
    {
        TArray<UWidgetComponent*> Widgets; ContinueActor->GetComponents(Widgets);
        TArray<FVector> Original; for (UWidgetComponent* Widget : Widgets) Original.Add(Widget->GetRelativeLocation());
        Manager->ChatActor = World->SpawnActor<AActor>(); Manager->bTrainingUIReady = true;
        Manager->ContinuePopupOffset = FVector(15, -20, 35);
        Manager->UpdateTrainingUI(); Manager->UpdateTrainingUI();
        TestEqual(TEXT("Both legacy Continue surfaces resolved"), Widgets.Num(), 2);
        for (int32 Index = 0; Index < Widgets.Num(); ++Index)
            TestEqual(TEXT("Continue offset applies to every surface without drift"), Widgets[Index]->GetRelativeLocation(), Original[Index] + Manager->ContinuePopupOffset);
        Manager->bTrainingUIReady = false; Manager->ChatActor = nullptr;
    }
    Belt->SetInteractionPatient(Patient);
    Manager->UpdateTrainingUI();
    TestFalse(TEXT("Entry quiz locks patient"), Patient->IsGrabInteractionEnabled());
    TestFalse(TEXT("Entry quiz locks loose belt"), Belt->IsGrabInteractionEnabled());
    TestTrue(TEXT("Entry quiz permits movement and turning"), Manager->IsTrainingMovementAllowed());
    TestEqual(TEXT("Default movement radius is two metres"), Manager->TrainingMovementRadiusMeters, 2.f);
    UClass* QuizClass = LoadClass<AActor>(nullptr, TEXT("/Game/Project/Blueprints/Actors/BP_QuizActor.BP_QuizActor_C"));
    Manager->QuizActor = QuizClass ? World->SpawnActor<AActor>(QuizClass) : nullptr;
    UWidgetComponent* QuizPanel = Cast<UWidgetComponent>(UCNAReliabilityLibrary::ObjectProperty(Manager->QuizActor, TEXT("QuizPanel")));
    if (!TestNotNull(TEXT("Actual quiz anchor"), QuizPanel)) { World->DestroyWorld(false); return false; }
    QuizPanel->SetWorldLocation(FVector::ZeroVector);
    Manager->TrainingMovementRadiusMeters = 5.f;
    TestTrue(TEXT("Five metre boundary accepted"), Manager->IsTrainingLocationAllowed(FVector(500, 0, 200)));
    TestFalse(TEXT("Destination beyond five metres rejected"), Manager->IsTrainingLocationAllowed(FVector(500.1, 0, 0)));
    TestFalse(TEXT("Diagonal distance cannot bypass radius"), Manager->IsTrainingLocationAllowed(FVector(400, 400, 0)));
    Manager->TrainingMovementRadiusMeters = 2.f;
    TestFalse(TEXT("Editor-adjusted radius applied immediately"), Manager->IsTrainingLocationAllowed(FVector(201, 0, 0)));
    APawn* Pawn = World->SpawnActor<APawn>();
    USceneComponent* Root = NewObject<USceneComponent>(Pawn); Pawn->SetRootComponent(Root); Pawn->AddInstanceComponent(Root); Root->RegisterComponent();
    const FVector Pelvis = Patient->GetPelvisLocation();
    Pawn->SetActorLocation(Pelvis - FVector(100, 0, 0));
    TestTrue(TEXT("Carry popup sits beside the moving patient"), Manager->CarryStatusLocation(Pawn).Equals(Pelvis + FVector(0, 85, 65), .01f));
    Pawn->SetActorLocation(Pelvis + FVector(100, 0, 0));
    TestTrue(TEXT("Carry offset follows viewer orientation without entering patient"), Manager->CarryStatusLocation(Pawn).Equals(Pelvis + FVector(0, -85, 65), .01f));
    Pawn->SetActorLocation(FVector(190, 0, 0));
    TestTrue(TEXT("Smooth move clipped at boundary"), Manager->ClampTrainingMovementDelta(Pawn, FVector(40, 0, 0)).Equals(FVector(10, 0, 0), .01f));
    Pawn->SetActorLocation(FVector(210, 0, 0));
    TestTrue(TEXT("Physical tracking outside boundary may move inward"), Manager->ClampTrainingMovementDelta(Pawn, FVector(-5, 0, 0)).Equals(FVector(-5, 0, 0)));
    TestTrue(TEXT("Outward movement from outside boundary blocked"), Manager->ClampTrainingMovementDelta(Pawn, FVector(5, 0, 0)).IsZero());
    QuizPanel->SetWorldLocation(FVector(1000, 2000, 120));
    TestTrue(TEXT("Circle follows quiz panel, not player arrival"), Manager->IsTrainingLocationAllowed(FVector(1000, 2000, 0)));
    TestFalse(TEXT("Previous player-centred area no longer accepted"), Manager->IsTrainingLocationAllowed(FVector::ZeroVector));
    Manager->bWrongAnswerOpen = true;
    TestFalse(TEXT("Wrong answer popup suspends lesson boundary"), Manager->IsLessonBoundaryActive());
    TestTrue(TEXT("Wrong answer popup permits distant teleport"), Manager->IsTrainingLocationAllowed(FVector(9000, 9000, 0)));
    TestEqual(TEXT("Wrong answer popup permits full smooth movement"), Manager->ClampTrainingMovementDelta(Pawn, FVector(40, 0, 0)), FVector(40, 0, 0));
    Manager->bWrongAnswerOpen = false;
    TestTrue(TEXT("Dismissed popup restores quiz boundary"), Manager->IsLessonBoundaryActive());
    Manager->bShowAllowedWalkHologram = false;
    TestTrue(TEXT("Hologram toggle does not disable radius enforcement"), Manager->IsLessonBoundaryActive());
    TestNotNull(TEXT("Floor material is a cookable manager asset reference"), Manager->WalkHologramMaterial.Get());
    Manager->TrainingMovementRadiusMeters = 5.f;
    Manager->SetTrainingStage(EGuidedTrainingStage::BathroomTransition);
    TestFalse(TEXT("Room fade transition still blocks movement"), Manager->IsTrainingMovementAllowed());
    Manager->SetTrainingStage(EGuidedTrainingStage::BathroomVideo);
    TestTrue(TEXT("Bathroom video permits movement and turning"), Manager->IsTrainingMovementAllowed());
    UTrainingMediaControllerComponent* Media = NewObject<UTrainingMediaControllerComponent>(Manager);
    Manager->AddInstanceComponent(Media); Media->RegisterComponent(); Manager->LessonMediaController = Media;
    Media->Session.State = FTrainingPlaybackSession::EState::Playing;
    TestTrue(TEXT("Playing video enables lesson circle"), Manager->IsLessonBoundaryActive());
    for (const auto State : { FTrainingPlaybackSession::EState::Opening, FTrainingPlaybackSession::EState::RetryPending, FTrainingPlaybackSession::EState::Failed })
    {
        Media->Session.State = State;
        TestFalse(TEXT("Media loading or recovery popup has no lesson boundary"), Manager->IsLessonBoundaryActive());
        TestTrue(TEXT("Media recovery popup permits distant movement"), Manager->IsTrainingLocationAllowed(FVector(9000, 9000, 0)));
    }
    Manager->SetTrainingStage(EGuidedTrainingStage::TransferVideo);
    TestTrue(TEXT("Transfer video permits movement and turning"), Manager->IsTrainingMovementAllowed());
    TestFalse(TEXT("Premature conversation blocked"), Manager->BeginConversationResponse());
    TestFalse(TEXT("Premature Continue blocked"), Manager->BeginRoomTransition());
    Manager->SetTrainingStage(EGuidedTrainingStage::Conversation);
    TestTrue(TEXT("Conversation popup has no distance restriction"), Manager->IsTrainingLocationAllowed(FVector(9000, 9000, 0)));
    TestFalse(TEXT("Next before response blocked"), Manager->BeginTransferLesson());
    TestTrue(TEXT("First conversation response accepted"), Manager->BeginConversationResponse());
    TestFalse(TEXT("Repeated conversation response blocked"), Manager->BeginConversationResponse());
    Manager->SetTrainingStage(EGuidedTrainingStage::Transfer);
    TestTrue(TEXT("Quiz success enables support"), Patient->IsGrabInteractionEnabled());
    TestFalse(TEXT("Belt unavailable before sitting"), Belt->IsGrabInteractionEnabled());
    Patient->SetInteractionPhase(EPatientInteractionPhase::BedSeating);
    Manager->UpdateTrainingUI();
    TestTrue(TEXT("Bed seating permits movement and turning"), Manager->IsTrainingMovementAllowed());
    TestTrue(TEXT("Bed instructions have no distance restriction"), Manager->IsTrainingLocationAllowed(FVector(9000, 9000, 0)));
    Patient->SetInteractionPhase(EPatientInteractionPhase::BeltTransfer);
    Manager->UpdateTrainingUI();
    TestFalse(TEXT("Belt step denies direct patient grab"), Patient->IsGrabInteractionEnabled());
    TestTrue(TEXT("Belt step enables belt"), Belt->IsGrabInteractionEnabled());
    TestFalse(TEXT("Missing checkpoint cannot fabricate bed pose"), Patient->RestoreBedCheckpoint());
    UTransferStateMachine* Task = Manager->GetStateMachine();
    Task->Setup(TScriptInterface<IIPatient>(Patient), nullptr, Belt->GetBeltComponent(), Manager->GetScoring());
    Task->StartTask(); Task->ForceState(ETransferState::BeltLift);
    Manager->GetScoring()->AddPenalty(60.f, FText::FromString(TEXT("Test score headroom")));
    const float Score = Manager->GetScoring()->GetCurrentScore();
    const float Time = Task->GetElapsedTime();
    TestTrue(TEXT("Retry re-arms lift"), Task->RetryBeltLift());
    TestEqual(TEXT("Retry preserves score"), Manager->GetScoring()->GetCurrentScore(), Score);
    TestEqual(TEXT("Retry preserves elapsed time"), Task->GetElapsedTime(), Time);
    Task->ForceState(ETransferState::Complete);
    const float FinalScore = Manager->GetScoring()->GetCurrentScore();
    Task->ForceState(ETransferState::Complete); Task->ForceState(ETransferState::BeltLift); Task->StartTask();
    TestEqual(TEXT("Terminal task cannot restart or move backwards"), Task->GetCurrentState(), ETransferState::Complete);
    TestEqual(TEXT("Duplicate completion cannot award again"), Manager->GetScoring()->GetCurrentScore(), FinalScore);
    Manager->SetTrainingStage(EGuidedTrainingStage::Complete);
    Manager->SetTrainingStage(EGuidedTrainingStage::Transfer);
    TestEqual(TEXT("Completed guided stage stays terminal"), Manager->GetTrainingStage(), EGuidedTrainingStage::Complete);
    TestFalse(TEXT("Completed training denies belt"), Belt->IsGrabInteractionEnabled());
    TestFalse(TEXT("Completed task cannot retry lift"), Task->RetryBeltLift());
    UGameInstance* ResultInstance = NewObject<UGameInstance>(GEngine);
    ResultInstance->InitializeStandalone();
    ATransferManagerActor* ResultManager = ResultInstance->GetWorld()->SpawnActor<ATransferManagerActor>();
    ResultManager->bGuidedTraining = true;
    ResultManager->SetTrainingStage(EGuidedTrainingStage::Complete);
    ResultManager->UpdateStatus();
    if (TestNotNull(TEXT("Native final result panel created"), ResultManager->TrainingStatus.Get()))
    {
        TestEqual(TEXT("Native final surface grows for result padding"), ResultManager->TrainingStatus->GetDrawSize(), FVector2D(1200, 600));
        UUserWidget* ResultWidget = ResultManager->TrainingStatus->GetUserWidgetObject();
        UBorder* ResultBorder = ResultWidget && ResultWidget->WidgetTree ? Cast<UBorder>(ResultWidget->WidgetTree->FindWidget(TEXT("Border_102"))) : nullptr;
        if (TestNotNull(TEXT("Native final result background"), ResultBorder))
        {
            const FMargin Before = ResultBorder->GetPadding();
            TestEqual(TEXT("Final result reserves 24 extra bottom pixels"), Before.Bottom - Before.Top, 24.f);
            ResultManager->ResultPopupExtraBottomPadding = 60.f;
            ResultManager->UpdateStatus(); ResultManager->UpdateStatus();
            TestEqual(TEXT("Live bottom padding does not accumulate"), ResultBorder->GetPadding().Bottom - Before.Bottom, 36.f);
            TestEqual(TEXT("Larger editor padding grows final surface"), ResultManager->TrainingStatus->GetDrawSize(), FVector2D(1200, 636));
        }
    }
    ResultInstance->Shutdown();
    World->DestroyWorld(false);
    return true;
}
#endif
