#include "Patient/PatientActor.h"
#include "Patient/PatientBoneMapping.h"
#include "Transfer/BeltActor.h"
#include "Components/BeltComponent.h"
#include "Components/GrabComponent.h"
#include "Components/SeatedTransitionComponent.h"
#include "Components/PatientCinematicComponent.h"
#include "Camera/CameraComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Components/WidgetComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Reliability/CNAReliabilityLibrary.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Engine/Blueprint.h"
#include "Engine/Level.h"
#include "FileHelpers.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_FunctionEntry.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"
#include "UObject/Script.h"

#include "Components/TransferGuidanceComponent.h"
#include "Transfer/TransferManagerActor.h"
#include "Transfer/WheelchairActor.h"
#include "StateMachine/TransferStateMachine.h"
#include "StateMachine/States/WheelchairTransferState.h"
#include "Reliability/TransferHintMenuWidget.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/TextRenderComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
struct FInteractionTestWorld
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    APatientActor* Patient = nullptr;
    ABeltActor* Belt = nullptr;
    FInteractionTestWorld()
    {
        Patient = World->SpawnActor<APatientActor>(); Belt = World->SpawnActor<ABeltActor>();
        Belt->SetInteractionPatient(Patient);
        UPatientBoneMapping* Mapping = LoadObject<UPatientBoneMapping>(nullptr, TEXT("/Game/PatientSetup/DA_BoneMapping_SKM_Manny_Simple"));
        FindFProperty<FObjectPropertyBase>(Patient->GetClass(), TEXT("BoneMapping"))->SetObjectPropertyValue_InContainer(Patient, Mapping);
    }
    ~FInteractionTestWorld() { World->DestroyWorld(false); }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCNAPatientPhaseTest, "CNA.PatientInteraction.PhasePermissions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCNAPatientPhaseTest::RunTest(const FString& Parameters)
{
    FInteractionTestWorld Setup;
    const EPatientInteractionPhase Phases[] = { EPatientInteractionPhase::BedPreparation, EPatientInteractionPhase::BedSeating,
        EPatientInteractionPhase::BeltTransfer, EPatientInteractionPhase::WheelchairSeating, EPatientInteractionPhase::Complete };
    const bool Body[] = { true, false, false, false, false };
    const bool Belt[] = { true, false, true, false, false };
    TestTrue(TEXT("Unassociated belts retain ordinary interaction"), Setup.World->SpawnActor<ABeltActor>()->IsGrabInteractionEnabled());
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Phases); ++Index)
    {
        Setup.Patient->SetInteractionPhase(Phases[Index]);
        TestEqual(TEXT("Body permission follows phase"), Setup.Patient->IsGrabInteractionEnabled(), Body[Index]);
        TestEqual(TEXT("Head trace eligibility follows phase"), Setup.Patient->CanBeGrabbed(TEXT("head"), FVector::ZeroVector), Body[Index]);
        TestFalse(TEXT("Limbs are never added to original grab roles"), Setup.Patient->CanBeGrabbed(TEXT("upperarm_l"), FVector::ZeroVector));
        TestEqual(TEXT("Loose belt permission follows patient's phase"), Setup.Belt->CanBeGrabbed(NAME_None, FVector::ZeroVector), Belt[Index]);
        if (!Body[Index]) TestEqual(TEXT("Locked patient exposes no grab bodies"), Setup.Patient->GetGrabbableBoneNames().Num(), 0);
        if (!Belt[Index])
        {
            TestFalse(TEXT("Cannot attach belt during animation ownership"), Setup.Patient->CanAttachBelt());
            TestEqual(TEXT("Locked belt exposes no handles"), Setup.Belt->GetGrabbableBoneNames().Num(), 0);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCNAPatientCinematicFadeTest, "CNA.PatientInteraction.MobileVRFade", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCNAPatientCinematicFadeTest::RunTest(const FString& Parameters)
{
    FInteractionTestWorld Setup;
    UPatientCinematicComponent* Cinematic = Setup.Patient->GetPatientCinematicComponent();
    Cinematic->PreFadeDelay = 0.f;
    Cinematic->FadeOutDuration = 1.f;
    Cinematic->BlackScreenHoldDuration = 1.f;
    Cinematic->FadeInDuration = 1.f;

    APawn* Pawn = Setup.World->SpawnActor<APawn>();
    UCameraComponent* Camera = NewObject<UCameraComponent>(Pawn);
    Pawn->SetRootComponent(Camera); Pawn->AddInstanceComponent(Camera); Camera->RegisterComponent();
    APlayerController* PC = Setup.World->SpawnActor<APlayerController>(); Setup.World->AddController(PC); PC->SetPawn(Pawn);
    Cinematic->Initialize(Setup.Patient->GetPatientMesh(), NAME_None);
    Cinematic->PostFadeLocationOffset = FVector(11, 0, 0);
    Cinematic->PostFadeRelativeRotation = FRotator::ZeroRotator;
    const FVector Before = Setup.Patient->GetPatientMesh()->GetComponentLocation();
    Cinematic->bUseMeshFadeWithoutHMD = true;
    // Exercise the selector through the public sequence entry, not a manually
    // created overlay that would bypass the Android/HMD backend decision.
    Cinematic->StartCinematicSequence();
    if (!TestNotNull(TEXT("Sequence selected the mesh backend"), Cinematic->VRFadeSphere.Get())) return false;
    auto ReadOpacity = [&]() { float Value = -1; Cinematic->VRFadeMaterial->GetScalarParameterValue(TEXT("Opacity"), Value); return Value; };
    TestEqual(TEXT("Uses the proven menu material"), Cinematic->FadeSphereMaterial->GetPathName(), FString(TEXT("/Game/Project/Custom_Materails/M_VRFade.M_VRFade")));
    TestEqual(TEXT("Fade attached to active camera"), Cinematic->VRFadeSphere->GetAttachParent(), static_cast<USceneComponent*>(Camera));
    TestEqual(TEXT("Fade geometry matches menu scale"), Cinematic->VRFadeSphere->GetRelativeScale3D(), FVector::OneVector);
    TestFalse(TEXT("Does not own the menu camera fade"), Cinematic->bOwnsCameraFade);
    TestEqual(TEXT("Initially clear"), ReadOpacity(), 0.f);
    Cinematic->FadeStartTime = Setup.World->GetTimeSeconds() - .5f;
    Cinematic->TickComponent(.5f, LEVELTICK_All, nullptr);
    TestTrue(TEXT("Fade out half opacity"), FMath::IsNearlyEqual(ReadOpacity(), .5f));
    Cinematic->OnFadeOutComplete();
    TestEqual(TEXT("Fully black before any reposition"), ReadOpacity(), 1.f);
    TestEqual(TEXT("Patient has not moved on black submission frame"), Setup.Patient->GetPatientMesh()->GetComponentLocation(), Before);
    Cinematic->TickComponent(0.f, LEVELTICK_All, nullptr);
    TestTrue(TEXT("Reposition waits for a subsequent frame"), Cinematic->bRepositionPending);
    // Automation runs in one frame; simulate the counter seen after a render.
    Cinematic->BlackFrameNumber = GFrameCounter - 1;
    Cinematic->TickComponent(.016f, LEVELTICK_All, nullptr);
    TestFalse(TEXT("Black-frame gate consumed"), Cinematic->bRepositionPending);
    TestEqual(TEXT("Patient moved only after gate"), Setup.Patient->GetPatientMesh()->GetComponentLocation(), Before + FVector(11,0,0));
    TestEqual(TEXT("Black hold remains opaque"), ReadOpacity(), 1.f);
    Cinematic->BeginFadeIn();
    Cinematic->FadeStartTime = Setup.World->GetTimeSeconds() - .5f;
    Cinematic->TickComponent(.5f, LEVELTICK_All, nullptr);
    TestTrue(TEXT("Fade in half opacity"), FMath::IsNearlyEqual(ReadOpacity(), .5f));
    UMaterialInstanceDynamic* FirstMID = Cinematic->VRFadeMaterial;
    Cinematic->CancelCinematic();
    TestFalse(TEXT("Cancellation stops sequence"), Cinematic->IsCinematicActive());
    float Cleared = -1; FirstMID->GetScalarParameterValue(TEXT("Opacity"), Cleared);
    TestEqual(TEXT("Cancellation clears opacity"), Cleared, 0.f);
    TestNull(TEXT("Cancellation destroys sphere"), Cinematic->VRFadeSphere.Get());
    TestNull(TEXT("Cancellation releases MID"), Cinematic->VRFadeMaterial.Get());
    TestFalse(TEXT("No idle tick overhead"), Cinematic->IsComponentTickEnabled());
    TestFalse(TEXT("Cancellation removes fade timer"), Setup.World->GetTimerManager().IsTimerActive(Cinematic->FadeOutTimerHandle));
    Cinematic->StartCinematicSequence();
    TestNotNull(TEXT("Repeat sequence creates its own overlay"), Cinematic->VRFadeSphere.Get());
    TestTrue(TEXT("Each sequence uses independent MID"), Cinematic->VRFadeMaterial.Get() != FirstMID);
    Cinematic->OnFadeInComplete();
    TestNull(TEXT("Completion removes overlay"), Cinematic->VRFadeSphere.Get());
    TestFalse(TEXT("Completion stops sequence"), Cinematic->IsCinematicActive());
    Cinematic->StartCinematicSequence(); Cinematic->UnregisterComponent();
    TestNull(TEXT("Teardown removes overlay"), Cinematic->VRFadeSphere.Get());
    TestFalse(TEXT("Teardown removes timers"), Setup.World->GetTimerManager().IsTimerActive(Cinematic->FadeOutTimerHandle));
    Cinematic->RegisterComponent();
    Cinematic->FadeOutDuration = 0.f; Cinematic->BlackScreenHoldDuration = 0.f; Cinematic->FadeInDuration = 0.f;
    Cinematic->StartCinematicSequence();
    TestTrue(TEXT("Zero timing still waits for black frame"), Cinematic->bRepositionPending);
    Cinematic->BlackFrameNumber = GFrameCounter - 1; Cinematic->TickComponent(.016f, LEVELTICK_All, nullptr);
    TestFalse(TEXT("Zero timing finishes after frame gate"), Cinematic->IsCinematicActive());

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCNATransferGuidanceTest, "CNA.PatientInteraction.TransferGuidance", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCNATransferGuidanceTest::RunTest(const FString& Parameters)
{
    UGameInstance* Instance = NewObject<UGameInstance>(GEngine);
    Instance->InitializeStandalone();
    UWorld* World = Instance->GetWorld();
    APatientActor* Patient = World->SpawnActor<APatientActor>();
    ABeltActor* Belt = World->SpawnActor<ABeltActor>();
    AWheelchairActor* Chair = World->SpawnActor<AWheelchairActor>(); Chair->LockBrakes(); Chair->SetActorLocation(FVector(400,0,0));
    UPatientBoneMapping* Mapping = LoadObject<UPatientBoneMapping>(nullptr, TEXT("/Game/PatientSetup/DA_BoneMapping_SKM_Manny_Simple"));
    FindFProperty<FObjectPropertyBase>(Patient->GetClass(), TEXT("BoneMapping"))->SetObjectPropertyValue_InContainer(Patient, Mapping);
    Patient->GetPatientMesh()->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple")));
    APlayerController* PC = World->SpawnActor<APlayerController>();
    ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
    Instance->AddLocalPlayer(LocalPlayer, FPlatformUserId::CreateFromInternalId(0));
    World->AddController(PC); PC->SetPlayer(LocalPlayer);
    APawn* Pawn = World->SpawnActor<APawn>();
    UCameraComponent* Camera = NewObject<UCameraComponent>(Pawn); Pawn->SetRootComponent(Camera); Pawn->AddInstanceComponent(Camera); Camera->RegisterComponent(); PC->SetPawn(Pawn);
    UGrabComponent* Grab = NewObject<UGrabComponent>(Pawn); Pawn->AddInstanceComponent(Grab); Grab->RegisterComponent(); Grab->SetTraceOrigin(Camera);
    ATransferManagerActor* Manager = World->SpawnActor<ATransferManagerActor>(); Manager->DispatchBeginPlay();
    UTransferStateMachine* State = Manager->GetStateMachine();
    UTransferGuidanceComponent* Hints = Manager->GetTransferGuidance();
    auto Check = [&](const TCHAR* Description, ETransferHintStep Expected) { Hints->RefreshHints(); TestTrue(Description, Hints->GetHintStep() == Expected); };
    Check(TEXT("Launch shows accepted patient grab"), ETransferHintStep::GrabPatient);
    TArray<USkeletalMeshComponent*> Ghosts; Manager->GetComponents(Ghosts);
    TestEqual(TEXT("Only the seated-patient ghost exists; hand holograms removed"), Ghosts.Num(), 1);
    for (UPrimitiveComponent* Visual : TArray<UPrimitiveComponent*>{Hints->SeatedGhost.Get(), Hints->BeltGhost.Get(), Hints->Target.Get(), Hints->ArrowShaft.Get(), Hints->ArrowHead.Get(), Hints->Label.Get()})
    {
        if (!Visual) continue;
        TestTrue(TEXT("Guidance never participates in traces or physics"), Visual->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
        TestFalse(TEXT("Guidance never simulates"), Visual->IsSimulatingPhysics());
        TestFalse(TEXT("Guidance never influences navigation"), Visual->CanEverAffectNavigation());
        TestFalse(TEXT("Guidance casts no shadows"), Visual->CastShadow);
    }
    Patient->OnGrabbed(Grab, TEXT("neck_01"), Patient->GetPatientMesh()->GetSocketLocation(TEXT("neck_01")));
    Check(TEXT("Successful neck grab advances to lift"), ETransferHintStep::SitUp);
    Patient->OnReleased(Grab);
    Check(TEXT("Early release restores neck hint"), ETransferHintStep::GrabPatient);
    Patient->SetInteractionPhase(EPatientInteractionPhase::BedSeating);
    Check(TEXT("Bed animation lock wins over task instructions"), ETransferHintStep::None);
    Patient->SetInteractionPhase(EPatientInteractionPhase::BeltTransfer);
    Check(TEXT("Bed completion advances to loose belt"), ETransferHintStep::PickUpBelt);
    FObjectPropertyBase* Held = FindFProperty<FObjectPropertyBase>(Grab->GetClass(), TEXT("GrabbedActor"));
    Held->SetObjectPropertyValue_InContainer(Grab, Belt); Grab->OnGrabStarted.Broadcast(Belt, NAME_None, Belt->GetActorLocation());
    TestTrue(TEXT("Actual held-actor event advances to attachment"), Hints->GetHintStep() == ETransferHintStep::AttachBelt);
    TestEqual(TEXT("Attachment marker follows actual patient target"), Hints->Target->GetComponentLocation(), Patient->GetBeltAttachTransform().GetLocation());
    Held->SetObjectPropertyValue_InContainer(Grab, nullptr); Grab->OnGrabEnded.Broadcast(Belt);
    TestTrue(TEXT("Loose belt early release returns to pickup"), Hints->GetHintStep() == ETransferHintStep::PickUpBelt);
    // A supported patient permits the real belt attachment API.
    Patient->SetInteractionPhase(EPatientInteractionPhase::BedPreparation);
    Patient->OnGrabbed(Grab, TEXT("neck_01"), FVector::ZeroVector);
    Patient->SetInteractionPhase(EPatientInteractionPhase::BeltTransfer);
    TestTrue(TEXT("Real belt attachment succeeds"), Belt->GetBeltComponent()->AttachToPatient(Patient));
    Check(TEXT("Attached belt exposes front handle"), ETransferHintStep::GrabBeltHandle);
    TestEqual(TEXT("Handle visual uses authoritative front location"), Hints->Target->GetComponentLocation(), Belt->GetHandleWorldLocation());
    TestTrue(TEXT("Floating handle text sits forty centimetres away from the belt"),
        FMath::IsNearlyEqual(FVector::Dist2D(Hints->Label->GetComponentLocation(), Belt->GetHandleWorldLocation()), 40.f, .1f));
    Hints->VisualSettings.BeltHandleLabelOffset = FVector(65, 0, 30); Hints->UpdateVisuals(); Hints->UpdateVisuals();
    TestTrue(TEXT("Live handle text offset updates without moving interaction target or accumulating"),
        FMath::IsNearlyEqual(FVector::Dist2D(Hints->Label->GetComponentLocation(), Belt->GetHandleWorldLocation()), 65.f, .1f)
        && Hints->Target->GetComponentLocation().Equals(Belt->GetHandleWorldLocation(), .1f));
    Belt->GetBeltComponent()->OnHandleGrabbed(Grab, TEXT("BeltHandle_Front"), Belt->GetHandleWorldLocation());
    Check(TEXT("Acquired handle starts lift guidance"), ETransferHintStep::LiftAndCarry);
    TestEqual(TEXT("Before latching, configured ready chair is selected"), Hints->GetSelectedWheelchair(), Chair);
    AWheelchairActor* Alternative = World->SpawnActor<AWheelchairActor>(); Alternative->LockBrakes(); Alternative->SetActorLocation(FVector(150,0,0));
    Hints->RefreshHints(); TestEqual(TEXT("Nearest ready chair selected before approach"), Hints->GetSelectedWheelchair(), Alternative);
    Chair->UnlockBrakes(); Hints->RefreshHints();
    TestEqual(TEXT("Unavailable configured chair falls back to nearest ready chair"), Hints->GetSelectedWheelchair(), Alternative);
    Alternative->OnTransferComplete(Patient); Hints->RefreshHints();
    TestNull(TEXT("Occupied chair is excluded"), Hints->GetSelectedWheelchair());
    TestTrue(TEXT("No seating or release instruction without ready chair"), Hints->GetHintStep() == ETransferHintStep::LiftAndCarry);
    Alternative->Destroy(); Alternative = World->SpawnActor<AWheelchairActor>();
    Alternative->SetActorLocation(FVector(150,0,0)); Alternative->SetActorRotation(FRotator(0, 73, 0)); Alternative->LockBrakes(); Chair->LockBrakes();
    State->ForceState(ETransferState::WheelchairTransfer);
    Patient->SetInteractionPhase(EPatientInteractionPhase::BeltTransfer);
    Check(TEXT("Carry state guides toward seat"), ETransferHintStep::MoveToSeat);
    TestEqual(TEXT("Hint already targets the chair that will receive the patient"), Hints->GetSelectedWheelchair(), Alternative);
    TestTrue(TEXT("Cyan ghost is centred before entering the chair zone"), Hints->SeatedGhost->GetSocketLocation(Patient->GetPelvisBoneName()).Equals(Alternative->GetTargetSeatTransform().GetLocation(), .1f));
    // Exercise selection through the real state's approach-zone latch.
    const FVector Seat = Alternative->GetTargetSeatTransform().GetLocation();
    Patient->GetPatientMesh()->SetAllBodiesSimulatePhysics(false); Patient->GetPatientMesh()->RefreshBoneTransforms();
    Patient->GetPatientMesh()->AddWorldOffset(Seat - Patient->GetPelvisLocation(), false, nullptr, ETeleportType::TeleportPhysics);
    UWheelchairTransferState* ChairState = Cast<UWheelchairTransferState>(FindFProperty<FObjectPropertyBase>(State->GetClass(), TEXT("CurrentState"))->GetObjectPropertyValue_InContainer(State));
    if (TestNotNull(TEXT("Actual wheelchair transfer state active"), ChairState)) ChairState->TickState(0.f);
    Check(TEXT("Release shown only inside actual commit zone"), ETransferHintStep::ReleaseToSeat);
    TestEqual(TEXT("Hint follows actual latched chair, overriding configured chair"), Hints->GetSelectedWheelchair(), Alternative);
    TestTrue(TEXT("Seated ghost pelvis aligns to SeatTarget"), Hints->SeatedGhost->GetSocketLocation(Patient->GetPelvisBoneName()).Equals(Seat, .1f));
    TestTrue(TEXT("Ghost mesh yaw matches actual calibrated seated-patient yaw"), Hints->SeatedGhost->GetComponentQuat().Equals(Alternative->GetTargetSeatTransform().GetRotation(), .001f));
    Alternative->AddActorWorldOffset(FVector(10,0,0)); Alternative->AddActorWorldRotation(FRotator(0, 45, 0)); Hints->UpdateVisuals();
    TestTrue(TEXT("Moving chair updates ghost anchor every frame"), Hints->SeatedGhost->GetSocketLocation(Patient->GetPelvisBoneName()).Equals(Alternative->GetTargetSeatTransform().GetLocation(), .1f));
    Alternative->Destroy(); Hints->UpdateVisuals();
    TestEqual(TEXT("Removed latched chair immediately falls back to valid chair"), Hints->GetSelectedWheelchair(), Chair);
    TestTrue(TEXT("Removed chair cannot leave a release instruction"), Hints->GetHintStep() != ETransferHintStep::ReleaseToSeat);
    Belt->GetBeltComponent()->OnHandleReleased(Grab);
    Check(TEXT("Early handle release restores grab hint"), ETransferHintStep::GrabBeltHandle);
    Hints->SetHintsEnabled(false); Check(TEXT("Off hides guidance"), ETransferHintStep::None);
    UTransferGuidanceComponent* NextLevelGuidance = NewObject<UTransferGuidanceComponent>(Manager); NextLevelGuidance->RegisterComponent(); NextLevelGuidance->Initialize(State);
    TestFalse(TEXT("New guidance component retains session preference"), NextLevelGuidance->AreHintsEnabled());
    NextLevelGuidance->DestroyComponent(); Hints->SetHintsEnabled(true);
    Patient->SetInteractionPhase(EPatientInteractionPhase::WheelchairSeating); Check(TEXT("Chair seating lock hides hints"), ETransferHintStep::None);
    Patient->SetInteractionPhase(EPatientInteractionPhase::Complete); Check(TEXT("Completion retires guidance"), ETransferHintStep::None);
    Patient->SetInteractionPhase(EPatientInteractionPhase::BedPreparation);
    Patient->OnReleased(Grab); Belt->GetBeltComponent()->DetachFromPatient(); State->Reset();
    Check(TEXT("Repeated attempt returns to patient contact"), ETransferHintStep::GrabPatient);
    State->OnTransferFailed.Broadcast(FText::FromString(TEXT("Test task failure")));
    Check(TEXT("Failure event hides hints even before patient injury"), ETransferHintStep::None);
    Manager->ResetTransfer(); Check(TEXT("Reset clears failure hint lock"), ETransferHintStep::GrabPatient);
    Patient->SetPatientState(EPatientState::Injured); Check(TEXT("Failure hides guidance"), ETransferHintStep::None);
    Patient->SetPatientState(EPatientState::LyingDown);
    // Existing UMG cursor routing targets the normal button added to this menu.
    UClass* MenuClass = LoadClass<UTransferHintMenuWidget>(nullptr, TEXT("/Game/VRTemplate/Blueprints/WidgetMenu.WidgetMenu_C"));
    if (TestNotNull(TEXT("Existing menu uses new native parent"), MenuClass))
    {
        UTransferHintMenuWidget* Menu = CreateWidget<UTransferHintMenuWidget>(PC, MenuClass);
        if (TestNotNull(TEXT("Menu can be constructed"), Menu))
        {
            Menu->TakeWidget();
            UButton* Exit = Cast<UButton>(Menu->WidgetTree->FindWidget(TEXT("ExitButton")));
            UTextBlock* Quit = Exit ? Cast<UTextBlock>(Exit->GetContent()) : nullptr;
            TestTrue(TEXT("Exit button clearly says Quit"), Quit && Quit->GetText().ToString() == TEXT("Quit"));
            UButton* Button = Cast<UButton>(Menu->WidgetTree->FindWidget(TEXT("HintsButton")));
            UTextBlock* Label = Cast<UTextBlock>(Menu->WidgetTree->FindWidget(TEXT("HintsLabel")));
            if (TestNotNull(TEXT("Hints button added to existing menu"), Button) && TestNotNull(TEXT("Hints button label"), Label))
            {
                UClass* VRPawnClass = LoadClass<APawn>(nullptr, TEXT("/Game/VRTemplate/Blueprints/VRPawn.VRPawn_C"));
                APawn* VRPawn = VRPawnClass ? World->SpawnActor<APawn>(VRPawnClass) : nullptr;
                FObjectPropertyBase* MenuReference = VRPawn ? FindFProperty<FObjectPropertyBase>(VRPawnClass, TEXT("MenuReference")) : nullptr;
                if (TestNotNull(TEXT("Real VR pawn menu reference exists"), MenuReference))
                {
                    AActor* MenuActor = World->SpawnActor<AActor>(MenuReference->PropertyClass);
                    PC->SetPawn(VRPawn); MenuReference->SetObjectPropertyValue_InContainer(VRPawn, MenuActor);
                    Check(TEXT("Open menu hides transfer guidance"), ETransferHintStep::None);
                    MenuReference->SetObjectPropertyValue_InContainer(VRPawn, nullptr);
                    Check(TEXT("Closing menu restores current action"), ETransferHintStep::GrabPatient);
                    PC->SetPawn(Pawn); MenuActor->Destroy(); VRPawn->Destroy();
                }
                TestEqual(TEXT("Initial label"), Label->GetText().ToString(), FString(TEXT("Hints: On")));
                Button->OnClicked.Broadcast(); TestFalse(TEXT("Menu click changes session preference"), Hints->AreHintsEnabled());
                TestEqual(TEXT("Off label follows preference"), Label->GetText().ToString(), FString(TEXT("Hints: Off")));
                Button->OnClicked.Broadcast(); TestTrue(TEXT("Second menu click restores hints"), Hints->AreHintsEnabled());
            }
        }
    }
    Instance->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCNAPatientCompletionTest, "CNA.PatientInteraction.CompletionAndReleaseGuards", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCNAPatientCompletionTest::RunTest(const FString& Parameters)
{
    FInteractionTestWorld Setup;
    UGrabComponent* Hand = NewObject<UGrabComponent>(Setup.Patient);
    Setup.Patient->AddInstanceComponent(Hand); Hand->RegisterComponent();
    Setup.Patient->SetInteractionPhase(EPatientInteractionPhase::Complete);
    Setup.Patient->SetPatientState(EPatientState::BeingLifted);
    Setup.Patient->GetSeatedTransitionComponent()->OnSettleCancelled.Broadcast();
    Setup.Patient->SetInteractionPhase(EPatientInteractionPhase::BeltTransfer);
    Setup.Belt->GetBeltComponent()->OnHandleGrabbed(Hand, TEXT("BeltHandle_Front"), FVector::ZeroVector);
    Setup.Belt->GetBeltComponent()->OnHandleReleased(Hand);
    Setup.Belt->GetBeltComponent()->OnHandleReleased(Hand);
    TestTrue(TEXT("Completion cannot be undone by late callbacks"), Setup.Patient->GetInteractionPhase() == EPatientInteractionPhase::Complete);
    TestEqual(TEXT("Completed belt rejects direct handle callbacks"), Setup.Belt->GetBeltComponent()->GetActiveGrabCount(), 0);
    TestFalse(TEXT("Duplicate release cannot create a seating gesture"), Setup.Belt->GetBeltComponent()->HasPendingFinalHandleRelease());
    Setup.Patient->SetPatientState(EPatientState::LyingDown);
    TestTrue(TEXT("Explicit new-attempt state restores preparation permission"), Setup.Patient->IsGrabInteractionEnabled());
    TestTrue(TEXT("New attempt restores belt permission"), Setup.Belt->IsGrabInteractionEnabled());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCNAPatientRoomUITest, "CNA.PatientInteraction.RoomConversationDisabled", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCNAPatientRoomUITest::RunTest(const FString& Parameters)
{
    // Editor map actors do not execute Blueprint ProcessEvent without this guard.
    FEditorScriptExecutionGuard AllowBlueprintCallbacks;
    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(TEXT("/Game/Project/Maps/CNA_Map_01"));
    if (!TestNotNull(TEXT("Training map loads"), World)) return false;
    int32 Count = 0;
    for (AActor* Actor : World->PersistentLevel->Actors)
    {
        if (!Actor || Actor->GetClass()->GetName() != TEXT("BP_PatientChat_C")) continue;
        ++Count;
        FBoolProperty* EnabledFlag = FindFProperty<FBoolProperty>(Actor->GetClass(), TEXT("bEnablePatientConversation"));
        if (!TestNotNull(TEXT("Conversation enable flag exists"), EnabledFlag)) continue;
        TestTrue(TEXT("Delivery map retains enabled conversation"), EnabledFlag->GetPropertyValue_InContainer(Actor));
        const FBoolProperty* Preview = FindFProperty<FBoolProperty>(Actor->GetClass(), TEXT("bShowConversationForUITesting"));
        TestTrue(TEXT("Delivery preview flag is off"), Preview && !Preview->GetPropertyValue_InContainer(Actor));
        TestFalse(TEXT("Entry stage blocks premature conversation activation"), UCNAReliabilityLibrary::IsPatientConversationEnabled(Actor));
        UCNAReliabilityLibrary::CallNoArgs(Actor, TEXT("EnableChatPanel"));
        UWidgetComponent* Premature = Cast<UWidgetComponent>(UCNAReliabilityLibrary::ObjectProperty(Actor, TEXT("ChatPanel")));
        TestTrue(TEXT("Stage gate keeps premature panel noninteractive"), Premature && !Premature->IsVisible() && Premature->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
        // Exercise the disabled feature policy on this temporary loaded instance, never save it.
        EnabledFlag->SetPropertyValue_InContainer(Actor, false);
        UCNAReliabilityLibrary::CallNoArgs(Actor, TEXT("EnableChatPanel"));
        UWidgetComponent* Panel = Cast<UWidgetComponent>(UCNAReliabilityLibrary::ObjectProperty(Actor, TEXT("ChatPanel")));
        if (!TestNotNull(TEXT("ChatPanel reference retained"), Panel)) continue;
        TestFalse(TEXT("External activation cannot show disabled panel"), Panel->IsVisible());
        TestTrue(TEXT("Invisible panel has no pointer collision"), Panel->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
        const FBoolProperty* Flag = FindFProperty<FBoolProperty>(Actor->GetClass(), TEXT("bEnablePatientConversation"));
        TestTrue(TEXT("Conversation flag remains enabled by default outside this instance"), Flag && Flag->GetPropertyValue_InContainer(Actor->GetClass()->GetDefaultObject()));
        Panel->InitWidget();
        if (UObject* UI = Panel->GetUserWidgetObject())
        {
            if (FObjectPropertyBase* Owner = FindFProperty<FObjectPropertyBase>(UI->GetClass(), TEXT("OwningActor"))) Owner->SetObjectPropertyValue_InContainer(UI, Actor);
            TestFalse(TEXT("Disabled Interact callback is rejected"), UCNAReliabilityLibrary::BeginGuidedConversationResponse(UI));
            TestFalse(TEXT("Disabled Next callback is rejected"), UCNAReliabilityLibrary::BeginGuidedTransferLesson(UI));
        }
        EnabledFlag->SetPropertyValue_InContainer(Actor, true);
    }
    TestEqual(TEXT("Exactly one room conversation override"), Count, 1);
    UBlueprint* Widget = LoadObject<UBlueprint>(nullptr, TEXT("/Game/Project/Blueprints/UI/WBP_PatientChat"));
    if (!TestNotNull(TEXT("Patient widget loads"), Widget)) return false;
    int32 Buttons = 0;
    TArray<UEdGraph*> Graphs; Widget->GetAllGraphs(Graphs);
    for (UEdGraph* Graph : Graphs) for (UEdGraphNode* Node : Graph->Nodes)
        if (UK2Node_ComponentBoundEvent* Event = Cast<UK2Node_ComponentBoundEvent>(Node))
            if (Event->DelegatePropertyName == TEXT("OnClicked"))
            {
                ++Buttons;
                UEdGraphPin* Then = Event->FindPin(TEXT("then"));
                TestTrue(TEXT("Button callback enters conversation policy before lesson effects"), Then && Then->LinkedTo.Num() == 1
                    && (Then->LinkedTo[0]->GetOwningNode()->NodeComment == TEXT("CNA_GuidedGate")
                        || Then->LinkedTo[0]->GetOwningNode()->NodeComment == TEXT("CNA_GuidedCallback")));
            }
    TestEqual(TEXT("Both interact and next buttons are guarded"), Buttons, 2);
    return true;
}
#endif
