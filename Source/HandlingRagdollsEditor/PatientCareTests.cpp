#include "Misc/AutomationTest.h"
#include "Patient/PatientActor.h"
#include "Patient/PatientBoneMapping.h"
#include "Components/PatientBedSupportComponent.h"
#include "Components/PatientPhysicsComponent.h"
#include "Components/PatientCarryComponent.h"
#include "Components/SeatedTransitionComponent.h"
#include "Components/GrabComponent.h"
#include "Components/BeltComponent.h"
#include "Transfer/BeltActor.h"
#include "Transfer/WheelchairActor.h"
#include "StateMachine/TransferStateMachine.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/WorldSettings.h"
#include "Engine/Blueprint.h"
#include "Engine/SkeletalMesh.h"
#include "EdGraph/EdGraph.h"
#include "K2Node_CallFunction.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "AssetCompilingManager.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
struct FPatientFixture
{
	UWorld* World = nullptr;
	APatientActor* Patient = nullptr;
	ABeltActor* Belt = nullptr;
	AStaticMeshActor* Bed = nullptr;
	AWheelchairActor* Chair = nullptr;
	UGrabComponent* Hands[2] = {};
	USceneComponent* Origins[2] = {};
	FPatientFixture()
	{
		UWorld* Source = LoadObject<UWorld>(nullptr, TEXT("/Game/Project/Maps/CNA_Map_01.CNA_Map_01"));
		if (!Source) return;
		APatientActor* Template = nullptr;
		AStaticMeshActor* BedTemplate = nullptr;
		ABeltActor* BeltTemplate = nullptr;
		AWheelchairActor* ChairTemplate = nullptr;
		for (AActor* Actor : Source->PersistentLevel->Actors)
		{
			if (APatientActor* P = Cast<APatientActor>(Actor)) Template = P;
			if (ABeltActor* B = Cast<ABeltActor>(Actor)) BeltTemplate = B;
			if (AWheelchairActor* C = Cast<AWheelchairActor>(Actor))
				if (C->GetFName() == TEXT("WheelchairActor_2")) ChairTemplate = C;
			if (Actor && Actor->GetActorLabel() == TEXT("SM_Bed")) BedTemplate = Cast<AStaticMeshActor>(Actor);
		}
		if (!Template || !BedTemplate || !BeltTemplate) return;
		FAssetCompilingManager::Get().FinishAllCompilation();
		World = UWorld::CreateWorld(EWorldType::Game, false);
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.Template = BedTemplate;
		Bed = World->SpawnActor<AStaticMeshActor>(BedTemplate->GetActorLocation(), BedTemplate->GetActorRotation(), Params);
		Bed->SetActorTransform(BedTemplate->GetActorTransform(), false, nullptr, ETeleportType::TeleportPhysics);
		Params.Template = Template;
		Patient = World->SpawnActor<APatientActor>(Template->GetActorLocation(), Template->GetActorRotation(), Params);
		Patient->SetActorTransform(Template->GetActorTransform(), false, nullptr, ETeleportType::TeleportPhysics);
		Patient->GetBedSupportComponent()->BedActor = Bed;
		Params.Template = BeltTemplate;
		Belt = World->SpawnActor<ABeltActor>(BeltTemplate->GetActorLocation(), BeltTemplate->GetActorRotation(), Params);
		Belt->SetActorTransform(BeltTemplate->GetActorTransform(), false, nullptr, ETeleportType::TeleportPhysics);
		if (ChairTemplate)
		{
			Params.Template = ChairTemplate;
			Chair = World->SpawnActor<AWheelchairActor>(ChairTemplate->GetActorLocation(), ChairTemplate->GetActorRotation(), Params);
			Chair->SetActorTransform(ChairTemplate->GetActorTransform(), false, nullptr, ETeleportType::TeleportPhysics);
		}
		for (int32 I = 0; I < 2; ++I)
		{
			AActor* Hand = World->SpawnActor<AActor>();
			Origins[I] = NewObject<USceneComponent>(Hand);
			Hand->SetRootComponent(Origins[I]); Hand->AddInstanceComponent(Origins[I]); Origins[I]->RegisterComponent();
			Hands[I] = NewObject<UGrabComponent>(Hand);
			Hand->AddInstanceComponent(Hands[I]); Hands[I]->SetTraceOrigin(Origins[I]); Hands[I]->RegisterComponent();
		}
		World->InitializeActorsForPlay(FURL());
		World->GetWorldSettings()->NotifyBeginPlay();
		World->BeginPlay();
		UE_LOG(LogTemp, Display, TEXT("PatientFixture: actor=%s pelvis=%s begun=%d tick=%d"), *Patient->GetActorLocation().ToString(), *Patient->GetPelvisLocation().ToString(), World->HasBegunPlay(), Patient->GetBedSupportComponent()->PrimaryComponentTick.IsTickFunctionRegistered());
	}
	~FPatientFixture()
	{
		if (World)
		{
			World->BeginTearingDown();
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}
	}
	void Step(float Seconds, int32 FPS)
	{
		for (int32 I = 0; I < FMath::CeilToInt(Seconds * FPS); ++I) Tick(1.0f / FPS);
	}
	void Tick(float Delta)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, Delta);
	}
	bool Grab(int32 Hand, FName Bone)
	{
		Hands[Hand]->ReleaseRagdoll();
		FBodyInstance* Body = Patient->GetPatientMesh()->GetBodyInstance(Bone);
		const bool bFoot = Bone == TEXT("foot_l") || Bone == TEXT("foot_r");
		// Foot bone origins are ankle joints, which can lie inside the shin shape.
		// Place a scripted foot grip on the foot's actual physical body instead.
		Origins[Hand]->SetWorldLocation(bFoot && Body ? Body->GetCOMPosition() : Patient->GetPatientMesh()->GetBoneLocation(Bone));
		return Hands[Hand]->TryGrabRagdoll();
	}
	bool Move(int32 Hand, const FVector& Target, float Seconds, int32 FPS)
	{
		FVector Start = Origins[Hand]->GetComponentLocation();
		int32 Frames = FMath::CeilToInt(Seconds * FPS);
		for (int32 Frame = 1; Frame <= Frames; ++Frame)
		{
			Origins[Hand]->SetWorldLocation(FMath::Lerp(Start, Target, float(Frame) / Frames));
			Tick(1.0f / FPS);
			if (Patient->GetPelvisLocation().ContainsNaN() || Patient->GetPelvisVelocity().Size() > 1800.0f) return false;
		}
		return true;
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCNAPatientIdleTest, "CNA.PatientCare.UntouchedBedPose", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCNAPatientIdleTest::RunTest(const FString& Parameters)
{
    for (int32 FPS : {30, 60, 72})
    {
        FPatientFixture F;
        if (!TestNotNull(TEXT("Patient"), F.Patient)) return false;
        USkeletalMeshComponent* Mesh = F.Patient->GetPatientMesh();
        UPatientBedSupportComponent* Support = F.Patient->GetBedSupportComponent();
        const FTransform Bed = F.Bed->GetActorTransform();
        const FQuat InitialPelvis = Mesh->GetBoneQuaternion(TEXT("pelvis"));
        const FQuat InitialChest = Mesh->GetBoneQuaternion(TEXT("spine_05"));
        for (int32 Second = 0; Second <= 45; ++Second)
        {
            const FVector Pelvis = Bed.InverseTransformPosition(F.Patient->GetPelvisLocation());
            const FVector Head = Bed.InverseTransformPosition(Mesh->GetBoneLocation(TEXT("head")));
            const FVector Chest = Bed.InverseTransformPosition(Mesh->GetBoneLocation(TEXT("spine_05")));
            if (Second == 0 || Second == 5 || Second == 45)
                AddInfo(FString::Printf(TEXT("Idle %d FPS t=%d pelvis=%s chest=%s head=%s angle=%.1f speed=%.1f hipRotation=%.1f chestRotation=%.1f"), FPS, Second,
                    *Pelvis.ToString(), *Chest.ToString(), *Head.ToString(), F.Patient->GetSeatedTransitionComponent()->GetTorsoUprightAngleDeg(), F.Patient->GetPelvisVelocity().Size(),
                    FMath::RadiansToDegrees(InitialPelvis.AngularDistance(Mesh->GetBoneQuaternion(TEXT("pelvis")))), FMath::RadiansToDegrees(InitialChest.AngularDistance(Mesh->GetBoneQuaternion(TEXT("spine_05"))))));
            TestTrue(TEXT("Untouched hips stay on mattress rather than floating"), Pelvis.Z < Support->MattressMax.Z + 18.0f && Pelvis.Z > Support->MattressMax.Z - 3.0f);
            if (Second >= 5)
            {
                TestTrue(TEXT("Untouched torso lies flat"), FMath::Abs(Chest.Z - Pelvis.Z) < 15.0f);
                TestTrue(TEXT("Head remains resting above mattress"), Head.Z >= Support->MattressMax.Z - 3.0f && Head.Z <= Support->MattressMax.Z + 35.0f);
                TestTrue(TEXT("Untouched patient has low motion"), F.Patient->GetPelvisVelocity().Size() < 15.0f);
                TestTrue(TEXT("Untouched hips do not roll or twist"), FMath::RadiansToDegrees(InitialPelvis.AngularDistance(Mesh->GetBoneQuaternion(TEXT("pelvis")))) < 20.0f);
                TestTrue(TEXT("Untouched chest does not roll or twist"), FMath::RadiansToDegrees(InitialChest.AngularDistance(Mesh->GetBoneQuaternion(TEXT("spine_05")))) < 25.0f);
            }
            F.Step(1.0f, FPS);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCNAPatientAssetsTest, "CNA.PatientCare.AssetsAndQuizIsolation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCNAPatientAssetsTest::RunTest(const FString& Parameters)
{
	UPhysicsAsset* Physics = LoadObject<UPhysicsAsset>(nullptr, TEXT("/Game/Patient/PA_CNA_Patient.PA_CNA_Patient"));
	if (!TestNotNull(TEXT("Dedicated patient physics asset"), Physics)) return false;
	for (const TCHAR* Bone : { TEXT("pelvis"), TEXT("head"), TEXT("spine_05"), TEXT("hand_l"), TEXT("hand_r"), TEXT("foot_l"), TEXT("foot_r") })
		TestTrue(FString(TEXT("Grab body present: ")) + Bone, Physics->FindBodyIndex(Bone) != INDEX_NONE);
	TestEqual(TEXT("Body chain has one joint per non-root body"), Physics->ConstraintSetup.Num(), Physics->SkeletalBodySetups.Num() - 1);
	USkeletalMesh* SkeletonMesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
	if (!TestNotNull(TEXT("Existing patient skeleton"), SkeletonMesh)) return false;
	const FReferenceSkeleton& Ref = SkeletonMesh->GetRefSkeleton();
	TArray<FTransform> ComponentPose;
	ComponentPose.SetNum(Ref.GetNum());
	for (int32 Index = 0; Index < Ref.GetNum(); ++Index)
	{
		const int32 Parent = Ref.GetParentIndex(Index);
		ComponentPose[Index] = Parent == INDEX_NONE ? Ref.GetRefBonePose()[Index] : Ref.GetRefBonePose()[Index] * ComponentPose[Parent];
	}
	for (UPhysicsConstraintTemplate* Joint : Physics->ConstraintSetup)
	{
		TestEqual(TEXT("Joint cannot stretch"), Joint->DefaultInstance.GetLinearXMotion(), LCM_Locked);
		TestTrue(TEXT("Twist has a limit"), Joint->DefaultInstance.GetAngularTwistMotion() != ACM_Free);
		const FConstraintInstance& C = Joint->DefaultInstance;
		const FTransform ChildFrame = C.GetRefFrame(EConstraintFrame::Frame1) * ComponentPose[Ref.FindBoneIndex(C.ConstraintBone1)];
		const FTransform ParentFrame = C.GetRefFrame(EConstraintFrame::Frame2) * ComponentPose[Ref.FindBoneIndex(C.ConstraintBone2)];
		TestTrue(TEXT("Joint frames meet without positional mismatch in neutral pose"), ChildFrame.GetLocation().Equals(ParentFrame.GetLocation(), 0.01f));
		TestTrue(TEXT("Joint frames do not force a neutral limb to twist"), ChildFrame.GetRotation().Equals(ParentFrame.GetRotation(), 0.0001f));
		if (C.ConstraintBone1.ToString().StartsWith(TEXT("calf")))
		{
			TestTrue(TEXT("Knee hinge flexes across the body rather than twisting along the leg"), ChildFrame.GetUnitAxis(EAxis::X).Equals(FVector::ForwardVector, 0.001f));
			TestEqual(TEXT("Knee hinge swing is locked"), C.GetAngularSwing1Motion(), ACM_Locked);
		}
	}
	UBlueprint* Quiz = LoadObject<UBlueprint>(nullptr, TEXT("/Game/Project/Blueprints/UI/WBP_QuizPanel.WBP_QuizPanel"));
	if (!TestNotNull(TEXT("Actual quiz"), Quiz)) return false;
	TArray<UEdGraph*> Graphs; Quiz->GetAllGraphs(Graphs);
	for (UEdGraph* Graph : Graphs) for (UEdGraphNode* Node : Graph->Nodes)
		if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
			TestTrue(TEXT("Quiz never starts legacy patient seating"), Call->GetNodeTitle(ENodeTitleType::ListView).ToString() != TEXT("Play Player Sitting"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCNAPatientPhysicsTest, "CNA.PatientCare.ArticulatedGrabs", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCNAPatientPhysicsTest::RunTest(const FString& Parameters)
{
	for (int32 FPS : {30, 60, 72})
	{
		FPatientFixture F;
		if (!TestNotNull(TEXT("Placed patient template"), F.Patient)) return false;
		TestTrue(TEXT("Bed physical ownership initialized"), F.Patient->GetBedSupportComponent()->IsBedControlActive());
		F.Step(0.6f, FPS);
		for (const TCHAR* Bone : { TEXT("head"), TEXT("hand_l"), TEXT("hand_r"), TEXT("pelvis"), TEXT("calf_l"), TEXT("foot_r") })
			TestTrue(FString(TEXT("Body remains grabbable: ")) + Bone, F.Patient->CanBeGrabbed(Bone, FVector::ZeroVector));
		const float Mass = F.Patient->GetPatientMesh()->GetMass();
		const FVector PelvisStart = F.Patient->GetPelvisLocation();
		TestTrue(TEXT("Acquire left hand using actual contact trace"), F.Grab(0, TEXT("hand_l")));
		AddInfo(FString::Printf(TEXT("FPS %d selected left grab body %s"), FPS, *F.Hands[0]->GetGrabbedBone().ToString()));
		TestTrue(TEXT("Limb grab uses finite force limit"), F.Hands[0]->GetActiveForceLimit() > 0.0f && F.Hands[0]->GetActiveForceLimit() < 100000.0f);
		TestTrue(TEXT("Raised hand trajectory remains bounded"), F.Move(0, F.Origins[0]->GetComponentLocation() + FVector(0, 0, 40), 1.2f, FPS));
		TestTrue(TEXT("Moving a hand does not carry the patient away"), FVector::Dist(PelvisStart, F.Patient->GetPelvisLocation()) < 35.0f);
		TestTrue(TEXT("Acquire independent second limb"), F.Grab(1, TEXT("hand_r")));
		F.Step(0.2f, FPS);
		F.Hands[0]->ReleaseRagdoll();
		TestTrue(TEXT("Releasing one hand retains other grip"), F.Hands[1]->IsGrabbing());
		TestTrue(TEXT("Other arm retains its regional override"), F.Patient->GetBedSupportComponent()->GetRegionRelaxation(F.Hands[1]->GetGrabbedBone()) > 0.9f);
		F.Hands[1]->ReleaseRagdoll(); F.Step(0.5f, FPS);
		TestTrue(TEXT("Muscles recover after release"), F.Patient->GetBedSupportComponent()->GetRegionRelaxation(TEXT("hand_r")) < 0.01f);
		TestEqual(TEXT("Grabs do not alter patient mass"), F.Patient->GetPatientMesh()->GetMass(), Mass);
		F.Origins[0]->SetWorldLocation(FVector(5000, 5000, 5000));
		TestFalse(TEXT("No target at distant hand"), F.Hands[0]->TryGrabRagdoll());
		TestTrue(TEXT("Missed acquisition retains held grip"), F.Hands[0]->IsGripHeld());
		F.Origins[0]->SetWorldLocation(F.Patient->GetPatientMesh()->GetBoneLocation(TEXT("hand_l")));
		F.Step(0.2f, FPS);
		TestTrue(TEXT("Held grip acquires when a target enters reach"), F.Hands[0]->IsGrabbing());
		F.Hands[0]->SuspendInteraction();
		TestFalse(TEXT("Focus loss releases grip"), F.Hands[0]->IsGrabbing());
		TestFalse(TEXT("Focus loss clears latch"), F.Hands[0]->IsGripHeld());
		F.Hands[0]->ResumeInteraction();
		F.Patient->SetPatientState(EPatientState::Seated);
		TestTrue(TEXT("Bed seated task state does not disable physics"), F.Patient->GetPatientMesh()->IsSimulatingPhysics(TEXT("pelvis")));
		TestTrue(TEXT("Bed seated task state permits re-grab"), F.Grab(0, TEXT("hand_l")));
		if (F.Patient->GetPatientCarryComponent()->CanUseKinematicCarry())
		{
			F.Patient->PrepareForBeltCarry();
			TestFalse(TEXT("Carry handoff releases body constraint"), F.Hands[0]->IsGrabbing());
			TestFalse(TEXT("Carry handoff suspends bed forces"), F.Patient->GetBedSupportComponent()->IsBedControlActive());
		}
		AddInfo(FString::Printf(TEXT("Patient physics at %d FPS: pelvis=%s"), FPS, *F.Patient->GetPelvisLocation().ToString()));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCNAPatientSitUpTest, "CNA.PatientCare.EdgeSitUpTrajectories", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCNAPatientSitUpTest::RunTest(const FString& Parameters)
{
	for (int32 Attempt = 0; Attempt < 20; ++Attempt)
	{
	FPatientFixture F;
	if (!TestNotNull(TEXT("Patient"), F.Patient)) return false;
	const int32 Rates[] = {30, 60, 72};
	const int32 FPS = Rates[Attempt % 3];
	F.Step(1.0f, FPS);
	const FTransform Bed = F.Bed->GetActorTransform();
	const FVector HipTarget = Bed.TransformPosition(FVector(140, -217, 125));
	const FVector ChestTarget = HipTarget + FVector(0, 0, 50);
	TestTrue(TEXT("Acquire torso"), F.Grab(0, TEXT("spine_05")));
	TestTrue(TEXT("Acquire hips"), F.Grab(1, TEXT("pelvis")));
	AddInfo(FString::Printf(TEXT("Torso=%s hip=%s; initial pelvis=%s chest=%s"), *F.Hands[0]->GetGrabbedBone().ToString(), *F.Hands[1]->GetGrabbedBone().ToString(), *F.Patient->GetPelvisLocation().ToString(), *F.Patient->GetPatientMesh()->GetBoneLocation(TEXT("spine_05")).ToString()));
	const FVector Starts[2] = { F.Origins[0]->GetComponentLocation(), F.Origins[1]->GetComponentLocation() };
	for (int32 Frame = 1; Frame <= FPS * 4; ++Frame)
	{
		float Alpha = float(Frame) / (FPS * 4);
		F.Origins[0]->SetWorldLocation(FMath::Lerp(Starts[0], ChestTarget, Alpha));
		F.Origins[1]->SetWorldLocation(FMath::Lerp(Starts[1], HipTarget, Alpha));
		F.Tick(1.0f / FPS);
	}
	F.Hands[1]->ReleaseRagdoll();
	for (const TCHAR* Foot : { TEXT("foot_l"), TEXT("foot_r") })
	{
		TestTrue(FString(TEXT("Acquire ")) + Foot, F.Grab(1, Foot));
		TestEqual(TEXT("Foot trajectory acquires the actual foot body"), F.Hands[1]->GetGrabbedBone(), FName(Foot));
		const FVector OverEdge = HipTarget + Bed.TransformVector(FVector(0, -65, 0));
		F.Move(1, OverEdge + FVector(0, 0, 15), 2.5f, FPS);
		F.Move(1, OverEdge - FVector(0, 0, 75), 2.0f, FPS);
		F.Hands[1]->ReleaseRagdoll();
	}
	F.Step(2.0f, FPS);
	AddInfo(FString::Printf(TEXT("Final pelvis local=%s angle=%.1f speed=%.1f feet=%s / %s stable=%.2f"), *Bed.InverseTransformPosition(F.Patient->GetPelvisLocation()).ToString(), F.Patient->GetSeatedTransitionComponent()->GetTorsoUprightAngleDeg(), F.Patient->GetPelvisVelocity().Size(), *Bed.InverseTransformPosition(F.Patient->GetPatientMesh()->GetBoneLocation(TEXT("foot_l"))).ToString(), *Bed.InverseTransformPosition(F.Patient->GetPatientMesh()->GetBoneLocation(TEXT("foot_r"))).ToString(), F.Patient->GetBedSupportComponent()->GetStableTime()));
	TestTrue(TEXT("Guided torso and separate legs reach stable edge seating"), F.Patient->IsBedSeated());
	F.Hands[0]->ReleaseRagdoll();
	F.Step(1.0f, FPS);
	AddInfo(FString::Printf(TEXT("Released pelvis=%s angle=%.1f speed=%.1f feet=%s / %s"), *Bed.InverseTransformPosition(F.Patient->GetPelvisLocation()).ToString(), F.Patient->GetSeatedTransitionComponent()->GetTorsoUprightAngleDeg(), F.Patient->GetPelvisVelocity().Size(), *Bed.InverseTransformPosition(F.Patient->GetPatientMesh()->GetBoneLocation(TEXT("foot_l"))).ToString(), *Bed.InverseTransformPosition(F.Patient->GetPatientMesh()->GetBoneLocation(TEXT("foot_r"))).ToString()));
	TestTrue(TEXT("Weak seated balance remains after releasing torso"), F.Patient->IsBedSeated());
	TestTrue(TEXT("Seated limbs remain movable"), F.Grab(1, TEXT("hand_l")));
	F.Hands[1]->ReleaseRagdoll();
	TestTrue(TEXT("Belt attaches to stable edge seated patient"), F.Belt->GetBeltComponent()->AttachToPatient(F.Patient));
	TestTrue(TEXT("Belt attachment keeps bed support active"), F.Patient->GetBedSupportComponent()->IsBedControlActive());
	TestTrue(TEXT("Body manipulation remains available with belt attached"), F.Grab(0, TEXT("foot_r")));
	F.Hands[1]->ReleaseRagdoll();
	F.Origins[1]->SetWorldLocation(F.Belt->GetHandleWorldLocation());
	TestTrue(TEXT("Attached belt handle acquired"), F.Hands[1]->TryGrabRagdoll());
	TestEqual(TEXT("Handle has priority near waist"), F.Hands[1]->GetGrabbedActor(), static_cast<AActor*>(F.Belt));
	TestTrue(TEXT("Existing carry owns the patient"), F.Patient->IsKinematicCarryActive());
	TestFalse(TEXT("Carry releases other hand's body constraint"), F.Hands[0]->IsGrabbing());
	TestFalse(TEXT("Carry disables bed force ownership"), F.Patient->GetBedSupportComponent()->IsBedControlActive());
	if (Attempt == 19 && F.Patient->IsKinematicCarryActive())
	{
		UTransferStateMachine* Transfer = NewObject<UTransferStateMachine>(F.Hands[0]->GetOwner());
		F.Hands[0]->GetOwner()->AddInstanceComponent(Transfer);
		Transfer->RegisterComponent();
		Transfer->Setup(TScriptInterface<IIPatient>(F.Patient), F.Chair, F.Belt->GetBeltComponent(), nullptr);
		Transfer->StartTask();
		Transfer->ForceState(ETransferState::WheelchairTransfer);
		F.Step(0.2f, FPS);
		const FVector Delta = F.Chair->GetTargetSeatTransform().GetLocation() - F.Patient->GetPelvisLocation();
		F.Move(1, F.Origins[1]->GetComponentLocation() + Delta, 2.0f, FPS);
		F.Step(1.0f, FPS);
		F.Hands[1]->ReleaseRagdoll();
		F.Step(0.6f, FPS);
		TestTrue(TEXT("Existing chair recognition and release seats the patient"), F.Chair->IsOccupied());
		TestTrue(TEXT("Final wheelchair pose is animation owned"), F.Patient->GetSeatedTransitionComponent()->IsSeatedLocked());
		TestTrue(TEXT("Final pelvis aligns with selected chair"), FVector::Dist(F.Patient->GetPelvisLocation(), F.Chair->GetTargetSeatTransform().GetLocation()) < 1.0f);
	}
	AddInfo(FString::Printf(TEXT("Attempt %d/20 at %d FPS completed"), Attempt + 1, FPS));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCNABeltHeldAttachTest, "CNA.PatientCare.HeldBeltAttachment", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCNABeltHeldAttachTest::RunTest(const FString& Parameters)
{
	FPatientFixture F;
	if (!TestNotNull(TEXT("Patient"), F.Patient)) return false;
	F.Step(0.5f, 72);
	TestTrue(TEXT("Support head for existing attachment policy"), F.Grab(0, TEXT("head")));
	F.Origins[1]->SetWorldLocation(F.Belt->GetActorLocation());
	TestTrue(TEXT("Acquire loose belt"), F.Hands[1]->TryGrabRagdoll());
	TestEqual(TEXT("Loose belt acquired"), F.Hands[1]->GetGrabbedActor(), static_cast<AActor*>(F.Belt));
	TestTrue(TEXT("Belt attaches while physical grip remains held"), F.Belt->GetBeltComponent()->AttachToPatient(F.Patient));
	F.Origins[1]->SetWorldLocation(F.Belt->GetHandleWorldLocation());
	F.Tick(1.0f / 72);
	TestTrue(TEXT("Attachment migration retains grip latch"), F.Hands[1]->IsGripHeld());
	TestFalse(TEXT("Loose constraint retirement cannot arm chair seating"), F.Belt->GetBeltComponent()->HasPendingFinalHandleRelease());
	F.Step(0.3f, 72);
	TestTrue(TEXT("Held grip acquires attached handle"), F.Hands[1]->IsGrabbing());
	TestTrue(TEXT("Held attachment handoff starts existing carry"), F.Patient->IsKinematicCarryActive());
	TestFalse(TEXT("Body grip releases at carry ownership change"), F.Hands[0]->IsGrabbing());
	return true;
}
#endif
