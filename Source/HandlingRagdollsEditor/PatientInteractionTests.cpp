#include "Patient/PatientActor.h"
#include "Patient/PatientBoneMapping.h"
#include "Transfer/BeltActor.h"
#include "Components/BeltComponent.h"
#include "Components/GrabComponent.h"
#include "Components/SeatedTransitionComponent.h"
#include "Components/PatientCinematicComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetRenderingLibrary.h"
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

    // Force the compositor backend in a rendered desktop test without requiring
    // an attached headset. Read back the actual texture submitted to both eyes.
    if (!TestTrue(TEXT("Runtime fade texture/layer can be created without cooked assets"), Cinematic->CreateVRFadeOverlay())) return false;
    auto ReadOpacity = [&]()
    {
        return UKismetRenderingLibrary::ReadRenderTargetRawPixel(Cinematic, Cinematic->VRFadeTexture, 0, 0).A / 255.f;
    };
    Cinematic->StartCinematicSequence();
    Cinematic->UpdateVRFadeOverlay(0.f);
    TestEqual(TEXT("Overlay initially transparent"), ReadOpacity(), 0.f);
    Cinematic->FadeStartTime = Setup.World->GetTimeSeconds() - .5f;
    Cinematic->TickComponent(.5f, LEVELTICK_All, nullptr);
    TestTrue(TEXT("Fade out submits half opacity"), FMath::IsNearlyEqual(ReadOpacity(), .5f, .01f));
    Cinematic->OnFadeOutComplete();
    TestEqual(TEXT("Reposition begins with fully opaque overlay"), ReadOpacity(), 1.f);
    Cinematic->TickComponent(.5f, LEVELTICK_All, nullptr);
    TestEqual(TEXT("Overlay remains opaque during black hold"), ReadOpacity(), 1.f);
    Cinematic->BeginFadeIn();
    Cinematic->FadeStartTime = Setup.World->GetTimeSeconds() - .5f;
    Cinematic->TickComponent(.5f, LEVELTICK_All, nullptr);
    TestTrue(TEXT("Fade in submits half opacity"), FMath::IsNearlyEqual(ReadOpacity(), .5f, .01f));
    Cinematic->CancelCinematic();
    TestFalse(TEXT("Cancellation stops sequence"), Cinematic->IsCinematicActive());
    TestNull(TEXT("Cancellation destroys native layer component"), Cinematic->VRFadeLayer.Get());
    TestNull(TEXT("Cancellation releases render target"), Cinematic->VRFadeTexture.Get());
    TestFalse(TEXT("No idle tick overhead"), Cinematic->IsComponentTickEnabled());

    // A new sequence must be able to create and retire another overlay.
    TestTrue(TEXT("Overlay can be recreated for next attempt"), Cinematic->CreateVRFadeOverlay());
    Cinematic->StartCinematicSequence();
    Cinematic->OnFadeInComplete();
    TestNull(TEXT("Normal completion destroys overlay"), Cinematic->VRFadeLayer.Get());
    TestFalse(TEXT("Normal completion stops sequence"), Cinematic->IsCinematicActive());
    Cinematic->ClearAllTimers();

    TestTrue(TEXT("Overlay can be recreated before teardown"), Cinematic->CreateVRFadeOverlay());
    Cinematic->StartCinematicSequence();
    Cinematic->UnregisterComponent();
    TestNull(TEXT("Component teardown destroys overlay"), Cinematic->VRFadeLayer.Get());
    TestFalse(TEXT("Component teardown cancels timers"), Setup.World->GetTimerManager().IsTimerActive(Cinematic->FadeOutTimerHandle));

    Cinematic->FadeOutDuration = 0.f;
    Cinematic->BlackScreenHoldDuration = 0.f;
    Cinematic->FadeInDuration = 0.f;
    Cinematic->StartCinematicSequence();
    TestFalse(TEXT("Zero-duration sequence completes instead of waiting on an inactive timer"), Cinematic->IsCinematicActive());
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
    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(TEXT("/Game/Project/Maps/CNA_Map_01"));
    if (!TestNotNull(TEXT("Training map loads"), World)) return false;
    int32 Count = 0;
    for (AActor* Actor : World->PersistentLevel->Actors)
    {
        if (!Actor || Actor->GetClass()->GetName() != TEXT("BP_PatientChat_C")) continue;
        ++Count;
        TestFalse(TEXT("Placed conversation is disabled"), UCNAReliabilityLibrary::IsPatientConversationEnabled(Actor));
        UCNAReliabilityLibrary::CallNoArgs(Actor, TEXT("EnableChatPanel"));
        UWidgetComponent* Panel = Cast<UWidgetComponent>(UCNAReliabilityLibrary::ObjectProperty(Actor, TEXT("ChatPanel")));
        if (!TestNotNull(TEXT("ChatPanel reference retained"), Panel)) continue;
        TestFalse(TEXT("External activation cannot show disabled panel"), Panel->IsVisible());
        TestTrue(TEXT("Invisible panel has no pointer collision"), Panel->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
        const FBoolProperty* Flag = FindFProperty<FBoolProperty>(Actor->GetClass(), TEXT("bEnablePatientConversation"));
        TestTrue(TEXT("Conversation flag remains enabled by default outside this instance"), Flag && Flag->GetPropertyValue_InContainer(Actor->GetClass()->GetDefaultObject()));
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
                    && Then->LinkedTo[0]->GetOwningNode()->NodeComment == TEXT("PatientConversationPolicy"));
            }
    TestEqual(TEXT("Both interact and next buttons are guarded"), Buttons, 2);
    return true;
}
#endif
