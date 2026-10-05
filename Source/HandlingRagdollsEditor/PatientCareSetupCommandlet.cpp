#include "PatientCareSetupCommandlet.h"
#include "Patient/PatientActor.h"
#include "Patient/PatientBoneMapping.h"
#include "Components/PatientBedSupportComponent.h"
#include "Components/PatientPhysicsComponent.h"
#include "Animation/AnimSequence.h"
#include "Components/PatientCinematicComponent.h"
#include "Components/SeatedTransitionComponent.h"
#include "Components/GrabComponent.h"
#include "Transfer/BeltActor.h"
#include "Transfer/TransferManagerActor.h"
#include "Transfer/WheelchairActor.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "MotionControllerComponent.h"
#include "Engine/SkeletalMesh.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_Event.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "K2Node_CallFunction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "FileHelpers.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "UObject/SavePackage.h"
#include "AssetCompilingManager.h"
#include "Math/RotationMatrix.h"

UPatientCareSetupCommandlet::UPatientCareSetupCommandlet()
{
	IsClient = false; IsServer = false; IsEditor = true; LogToConsole = true;
}

namespace
{
bool SavePatientAsset(UObject* Asset)
{
	UPackage* Package = Asset->GetOutermost();
	Package->MarkPackageDirty();
	FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError;
	const FString File = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	return UPackage::SavePackage(Package, Asset, *File, Args);
}

bool BackupPackage(UObject* Asset, const FString& Directory)
{
	const FString Source = FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(),
		Asset->IsA<UWorld>() ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension());
	const FString Backup = Directory / FPaths::GetCleanFilename(Source);
	if (IFileManager::Get().FileExists(*Backup)) return true;
	return IFileManager::Get().Copy(*Backup, *Source) == COPY_OK;
}

void SetReference(UObject* Object, FName Name, UObject* Value)
{
	if (FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Object->GetClass(), Name))
		Property->SetObjectPropertyValue_InContainer(Object, Value);
}

FString Describe(UBlueprint* BP)
{
	FString Text = BP->GetPathName() + TEXT("\n");
	TArray<UEdGraph*> Graphs; BP->GetAllGraphs(Graphs);
	for (UEdGraph* Graph : Graphs)
	{
		Text += TEXT("GRAPH ") + Graph->GetName() + TEXT("\n");
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			Text += TEXT(" NODE ") + Node->GetName() + TEXT(" ") + Node->GetNodeTitle(ENodeTitleType::ListView).ToString() + TEXT("\n");
			for (UEdGraphPin* Pin : Node->Pins)
			{
				Text += TEXT("  ") + Pin->PinName.ToString() + TEXT("=") + Pin->DefaultValue;
				if (Pin->DefaultObject) Text += TEXT(" OBJECT ") + Pin->DefaultObject->GetPathName();
				for (UEdGraphPin* Link : Pin->LinkedTo)
					Text += TEXT(" -> ") + Link->GetOwningNode()->GetName() + TEXT(".") + Link->PinName.ToString();
				Text += TEXT("\n");
			}
		}
	}
	return Text;
}
}

int32 UPatientCareSetupCommandlet::Main(const FString& Params)
{
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("PatientCare");
	IFileManager::Get().MakeDirectory(*Directory, true);
	const bool bRepairBedPose = FParse::Param(*Params, TEXT("RepairBedPose"));
	const bool bApply = FParse::Param(*Params, TEXT("Apply")) || bRepairBedPose;
	const FString Backup = Directory / (bRepairBedPose ? TEXT("BeforeLinkPoseFix") : TEXT("BeforeFix"));
	if (bApply) IFileManager::Get().MakeDirectory(*Backup, true);
	const TCHAR* Blueprints[] = {
		TEXT("/Game/Project/Blueprints/Actors/BP_PatientInteraction"),
		TEXT("/Game/Project/Blueprints/Actors/BP_BedInteraction"),
		TEXT("/Game/Project/Blueprints/Actors/BP_QuizActor"),
		TEXT("/Game/Project/Blueprints/UI/WBP_QuizPanel"),
		TEXT("/Game/Project/Blueprints/Actors/BP_MediaEventHandler"),
		TEXT("/Game/VRTemplate/Blueprints/VRPawn") };
	for (const TCHAR* Path : Blueprints)
	{
		if (UBlueprint* BP = LoadObject<UBlueprint>(nullptr, Path))
		{
			FFileHelper::SaveStringToFile(Describe(BP), *(Directory / (BP->GetName() + TEXT(".txt"))));
			if (bApply)
			{
				FKismetEditorUtilities::CompileBlueprint(BP);
				if (BP->Status == BS_Error) return 1;
			}
		}
	}
	UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(TEXT("/Game/Project/Maps/CNA_Map_01"));
	if (!World) return 1;
	APatientActor* Patient = nullptr;
	ABeltActor* Belt = nullptr;
	ATransferManagerActor* Manager = nullptr;
	AActor* Bed = nullptr;
	AWheelchairActor* Chair = nullptr;
	FString Inspection;
	for (AActor* Actor : World->PersistentLevel->Actors)
	{
		if (!Actor) continue;
		if (APatientActor* Found = Cast<APatientActor>(Actor)) Patient = Found;
		if (ABeltActor* Found = Cast<ABeltActor>(Actor)) Belt = Found;
		if (ATransferManagerActor* Found = Cast<ATransferManagerActor>(Actor)) Manager = Found;
		if (AWheelchairActor* Found = Cast<AWheelchairActor>(Actor))
			if (Found->GetFName() == TEXT("WheelchairActor_2")) Chair = Found;
		if (Actor->GetActorLabel() == TEXT("SM_Bed")) Bed = Actor;
		if (Actor->IsA<APatientActor>() || Actor->IsA<ATransferManagerActor>()
			|| Actor->GetClass()->GetName().Contains(TEXT("PatientInteraction"))
			|| Actor->GetClass()->GetName().Contains(TEXT("Quiz")))
		{
			Inspection += Actor->GetPathName() + TEXT("\n");
			for (TFieldIterator<FProperty> It(Actor->GetClass()); It; ++It)
			{
				if (It->GetOwnerClass()->GetName() == TEXT("Actor") || It->GetOwnerClass()->GetName() == TEXT("Object")) continue;
				FString Value; It->ExportText_InContainer(0, Value, Actor, nullptr, Actor, PPF_None);
				if (Value.Len() < 1500) Inspection += It->GetName() + TEXT("=") + Value + TEXT("\n");
			}
		}
	}
	if (!Patient || !Belt || !Manager || !Bed || !Chair) return 1;
	UPhysicsAsset* Original = Patient->GetPatientMesh()->GetPhysicsAsset();
	if (!Original) return 1;
	Inspection += TEXT("BED ") + Bed->GetActorTransform().ToString() + TEXT("\nPATIENT ") + Patient->GetActorTransform().ToString() + TEXT("\n");
	for (UObject* Object : { static_cast<UObject*>(Patient->GetPatientMesh()), static_cast<UObject*>(Patient->GetPatientPhysicsComponent()) })
	{
		for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
		{
			FString Value; It->ExportText_InContainer(0, Value, Object, nullptr, Object, PPF_None);
			if (Value.Len() < 1500) Inspection += Object->GetName() + TEXT(".") + It->GetName() + TEXT("=") + Value + TEXT("\n");
		}
	}
	for (const TCHAR* Name : { TEXT("AN_Patient_Sleeping"), TEXT("Retargeted_SleepingLaying_Sleeping"), TEXT("Laying_Sleeping") })
	{
		if (UAnimSequence* Anim = LoadObject<UAnimSequence>(nullptr, *(FString(TEXT("/Game/Animations/")) + Name)))
		{
			Inspection += FString::Printf(TEXT("ANIMATION %s skeleton=%s\n"), *Anim->GetName(), *GetNameSafe(Anim->GetSkeleton()));
			if (!bApply)
			{
				USkeletalMeshComponent* Mesh = Patient->GetPatientMesh();
				FAssetCompilingManager::Get().FinishAllCompilation();
				Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode); Mesh->SetAnimation(Anim);
				Mesh->Play(false); Mesh->SetPosition(0.0f); Mesh->SetPlayRate(0.0f);
				Mesh->TickAnimation(0.0f, false); Mesh->RefreshBoneTransforms();
				for (const TCHAR* Bone : { TEXT("root"), TEXT("pelvis"), TEXT("spine_05"), TEXT("head"), TEXT("hand_l"), TEXT("hand_r"), TEXT("calf_l"), TEXT("foot_l") })
					Inspection += FString::Printf(TEXT("POSE %s %s %s\n"), Name, Bone, *Mesh->GetSocketTransform(FName(Bone), RTS_Component).ToString());
			}
		}
	}
	for (USkeletalBodySetup* Body : Original->SkeletalBodySetups)
		Inspection += FString::Printf(TEXT("BODY %s capsules=%d spheres=%d boxes=%d\n"), *Body->BoneName.ToString(),
			Body->AggGeom.SphylElems.Num(), Body->AggGeom.SphereElems.Num(), Body->AggGeom.BoxElems.Num());
	for (UPhysicsConstraintTemplate* Constraint : Original->ConstraintSetup)
	{
		FConstraintInstance& C = Constraint->DefaultInstance;
		Inspection += TEXT("FRAMES ") + C.ConstraintBone1.ToString() + TEXT(" ") + C.GetRefFrame(EConstraintFrame::Frame1).ToString() + TEXT(" ") + C.GetRefFrame(EConstraintFrame::Frame2).ToString() + TEXT("\n");
		Inspection += FString::Printf(TEXT("JOINT %s %s %s swing1=%.1f swing2=%.1f twist=%.1f\n"),
			*C.JointName.ToString(), *C.ConstraintBone1.ToString(), *C.ConstraintBone2.ToString(),
			C.GetAngularSwing1Limit(), C.GetAngularSwing2Limit(), C.GetAngularTwistLimit());
	}
	FFileHelper::SaveStringToFile(Inspection, *(Directory / TEXT("Map.txt")));
	if (!bApply) { UE_LOG(LogTemp, Display, TEXT("PatientCare: Inspection complete.")); return 0; }

    if (!BackupPackage(World, Backup)) return 1;
    UPatientBoneMapping* Mapping = LoadObject<UPatientBoneMapping>(nullptr,
        TEXT("/Game/PatientSetup/DA_BoneMapping_SKM_Manny_Simple"));
    if (!Mapping || !BackupPackage(Mapping, Backup)) return 1;
    const TPair<EPatientBoneRole, FName> Additions[] = {
        {EPatientBoneRole::ForearmLeft, TEXT("lowerarm_l")}, {EPatientBoneRole::ForearmRight, TEXT("lowerarm_r")},
        {EPatientBoneRole::HandLeft, TEXT("hand_l")}, {EPatientBoneRole::HandRight, TEXT("hand_r")},
        {EPatientBoneRole::FootLeft, TEXT("foot_l")}, {EPatientBoneRole::FootRight, TEXT("foot_r")} };
    for (const auto& Entry : Additions)
    {
        FBoneRoleEntry* Existing = Mapping->Mappings.FindByPredicate([&](const FBoneRoleEntry& E) { return E.Role == Entry.Key; });
        if (!Existing) { FBoneRoleEntry E; E.Role = Entry.Key; E.BoneName = Entry.Value; Mapping->Mappings.Add(E); }
        else Existing->BoneName = Entry.Value;
    }
    if (!SavePatientAsset(Mapping)) return 1;
    const TCHAR* AssetPath = TEXT("/Game/Patient/PA_CNA_Patient.PA_CNA_Patient");
    UPhysicsAsset* Physics = LoadObject<UPhysicsAsset>(nullptr, AssetPath);
    if (!Physics)
    {
        UPackage* Package = CreatePackage(TEXT("/Game/Patient/PA_CNA_Patient"));
        Physics = DuplicateObject<UPhysicsAsset>(Original, Package, TEXT("PA_CNA_Patient"));
        Physics->SetFlags(RF_Public | RF_Standalone);
        FAssetRegistryModule::AssetCreated(Physics);
    }
    else if (!BackupPackage(Physics, Backup)) return 1;
    const FReferenceSkeleton& Ref = Patient->GetPatientMesh()->GetSkeletalMeshAsset()->GetRefSkeleton();
    TArray<FTransform> ReferenceComponentPose;
    ReferenceComponentPose.SetNum(Ref.GetNum());
    for (int32 Index = 0; Index < Ref.GetNum(); ++Index)
    {
        const int32 Parent = Ref.GetParentIndex(Index);
        ReferenceComponentPose[Index] = Parent == INDEX_NONE ? Ref.GetRefBonePose()[Index]
            : Ref.GetRefBonePose()[Index] * ReferenceComponentPose[Parent];
    }
    // The source has two additional calf-to-pelvis joints which form closed loops
    // with the thigh/knee chain. Keep only joints to the nearest physical ancestor.
    Physics->ConstraintSetup.RemoveAll([&](const TObjectPtr<UPhysicsConstraintTemplate>& Template)
    {
        const FConstraintInstance& C = Template->DefaultInstance;
        int32 Index = Ref.FindBoneIndex(C.ConstraintBone1);
        if (Index == INDEX_NONE) return true;
        Index = Ref.GetParentIndex(Index);
        while (Index != INDEX_NONE && Physics->FindBodyIndex(Ref.GetBoneName(Index)) == INDEX_NONE)
            Index = Ref.GetParentIndex(Index);
        return Index == INDEX_NONE || Ref.GetBoneName(Index) != C.ConstraintBone2;
    });
    for (UPhysicsConstraintTemplate* Template : Physics->ConstraintSetup)
    {
        FConstraintInstance& C = Template->DefaultInstance;
        const FString Bone = C.ConstraintBone1.ToString();
        const FTransform Child = ReferenceComponentPose[Ref.FindBoneIndex(C.ConstraintBone1)];
        const FTransform Parent = ReferenceComponentPose[Ref.FindBoneIndex(C.ConstraintBone2)];
        FVector TwistAxis = Child.GetUnitAxis(EAxis::X);
        if (Bone.StartsWith(TEXT("calf"))) TwistAxis = FVector::ForwardVector;
        else if (Bone.StartsWith(TEXT("lowerarm")))
        {
            const FName HandBone = Bone.EndsWith(TEXT("_l")) ? TEXT("hand_l") : TEXT("hand_r");
            const FVector ForearmDirection = (ReferenceComponentPose[Ref.FindBoneIndex(HandBone)].GetLocation() - Child.GetLocation()).GetSafeNormal();
            TwistAxis = FVector::CrossProduct(ForearmDirection, FVector(0, -1, 0)).GetSafeNormal();
        }
        // Both frames coincide in the neutral skeleton, rather than copying
        // mannequin frames authored for different joint freedoms. Hinge twist
        // is anatomical flexion, not rotation along the long bone.
        const FVector Secondary = FMath::Abs(TwistAxis.Z) < 0.95f ? FVector::UpVector : FVector::ForwardVector;
        const FTransform JointFrame(FRotationMatrix::MakeFromXY(TwistAxis, Secondary).ToQuat(), Child.GetLocation());
        C.SetRefFrame(EConstraintFrame::Frame1, JointFrame.GetRelativeTransform(Child));
        C.SetRefFrame(EConstraintFrame::Frame2, JointFrame.GetRelativeTransform(Parent));
        C.SetLinearXLimit(LCM_Locked, 0.0f); C.SetLinearYLimit(LCM_Locked, 0.0f); C.SetLinearZLimit(LCM_Locked, 0.0f);
        C.ProfileInstance.bDisableCollision = true;
        C.ProfileInstance.bEnableMassConditioning = true;
        C.ProfileInstance.bEnableProjection = true;
        C.ProfileInstance.ProjectionLinearTolerance = 5.0f;
        C.ProfileInstance.ProjectionAngularTolerance = 20.0f;
        C.ProfileInstance.ProjectionLinearAlpha = 0.1f;
        C.ProfileInstance.ProjectionAngularAlpha = 0.05f;
        C.AngularRotationOffset = FRotator::ZeroRotator;
        float Swing1 = 25.0f, Swing2 = 20.0f, Twist = 12.0f;
        if (Bone.StartsWith(TEXT("upperarm"))) { Swing1 = 100.0f; Swing2 = 80.0f; Twist = 70.0f; }
        else if (Bone.StartsWith(TEXT("thigh"))) { Swing1 = 80.0f; Swing2 = 50.0f; Twist = 30.0f; }
        else if (Bone.StartsWith(TEXT("lowerarm")) || Bone.StartsWith(TEXT("calf")))
        {
            Swing1 = 0.0f; Swing2 = 0.0f; Twist = 65.0f;
            C.AngularRotationOffset.Roll = 65.0f;
        }
        else if (Bone.StartsWith(TEXT("hand"))) { Swing1 = 25.0f; Swing2 = 45.0f; Twist = 30.0f; }
        else if (Bone.StartsWith(TEXT("foot"))) { Swing1 = 15.0f; Swing2 = 30.0f; Twist = 15.0f; }
        else if (Bone.StartsWith(TEXT("clavicle"))) { Swing1 = 20.0f; Swing2 = 20.0f; Twist = 15.0f; }
        else if (Bone.StartsWith(TEXT("neck")) || Bone == TEXT("head")) { Swing1 = 20.0f; Swing2 = 20.0f; Twist = 20.0f; }
        C.SetAngularSwing1Limit(Swing1 > 0.0f ? ACM_Limited : ACM_Locked, Swing1);
        C.SetAngularSwing2Limit(Swing2 > 0.0f ? ACM_Limited : ACM_Locked, Swing2);
        C.SetAngularTwistLimit(ACM_Limited, Twist);
        // Constraint templates serialize their default profile separately;
        // updating only DefaultInstance.ProfileInstance is discarded on save.
        Template->SetDefaultProfile(C);
        Physics->DisableCollision(Physics->FindBodyIndex(C.ConstraintBone1), Physics->FindBodyIndex(C.ConstraintBone2));
    }
    for (USkeletalBodySetup* Body : Physics->SkeletalBodySetups)
    {
        Body->PhysicsType = PhysType_Default;
        Body->DefaultInstance.SetMaxDepenetrationVelocity(150.0f);
        Body->DefaultInstance.LinearDamping = 2.0f;
        Body->DefaultInstance.AngularDamping = 4.0f;
    }
    Physics->UpdateBodySetupIndexMap();
    if (!SavePatientAsset(Physics)) return 1;
    if (bRepairBedPose)
    {
        UE_LOG(LogTemp, Display, TEXT("PatientCare: Neutral anatomical joint frames repaired; backup=%s."), *Backup);
        return 0;
    }
    Patient->GetPatientMesh()->SetPhysicsAsset(Physics);
    UPatientBedSupportComponent* Support = Patient->GetBedSupportComponent();
    Support->BedActor = Bed;
    Support->bEnabled = true;
    Support->MattressMin = FVector(12.0f, -232.0f, 72.0f);
    Support->MattressMax = FVector(271.0f, -10.0f, 108.0f);
    Support->EdgeMarker = FVector(140.0f, -232.0f, 108.0f);
    Support->EdgeOutward = FVector(0.0f, -1.0f, 0.0f);
    Patient->GetPatientCinematicComponent()->bEnabled = false;
    Patient->GetSeatedTransitionComponent()->bDisablePhysicsAfterBedBlend = false;
    SetReference(Manager, TEXT("PatientRef"), Patient);
    SetReference(Manager, TEXT("BeltRef"), Belt);
    SetReference(Manager, TEXT("WheelchairRef"), Chair);
    // Keep legacy objects alive for Blueprint/UI references, but eliminate a
    // second colliding patient and the old answer-driven pose controller.
    for (AActor* Actor : World->PersistentLevel->Actors)
    {
        if (!Actor) continue;
        if (Actor->GetActorLabel() == TEXT("Laying_Shaking_Head") || Actor->GetClass()->GetName() == TEXT("BP_Belt_C"))
        {
            Actor->SetActorHiddenInGame(true);
            Actor->SetActorEnableCollision(false);
            Actor->SetActorTickEnabled(false);
        }
        if (Actor->GetClass()->GetName() == TEXT("BP_PatientInteraction_C")) Actor->SetActorTickEnabled(false);
    }
    for (const TCHAR* Path : { TEXT("/Game/Project/Blueprints/UI/WBP_QuizPanel"), TEXT("/Game/Project/Blueprints/Actors/BP_PatientInteraction") })
    {
        UBlueprint* BP = LoadObject<UBlueprint>(nullptr, Path);
        if (!BP || !BackupPackage(BP, Backup)) return 1;
        TArray<UEdGraph*> Graphs; BP->GetAllGraphs(Graphs);
        for (UEdGraph* Graph : Graphs)
        {
            TArray<UEdGraphNode*> Remove;
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                if (BP->GetName() == TEXT("WBP_QuizPanel"))
                {
                    if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
                        if (Call->GetNodeTitle(ENodeTitleType::ListView).ToString() == TEXT("Play Player Sitting")) Remove.Add(Node);
                }
                else if (UK2Node_CustomEvent* Event = Cast<UK2Node_CustomEvent>(Node))
                {
                    if (Event->CustomFunctionName.ToString().Contains(TEXT("Sitting")))
                        if (UEdGraphPin* Then = Event->FindPin(TEXT("then"))) Then->BreakAllPinLinks();
                }
                else if (UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node))
                {
                    if (Graph->GetName().StartsWith(TEXT("TransitionTo")))
                        if (UEdGraphPin* Then = Entry->FindPin(TEXT("then"))) Then->BreakAllPinLinks();
                }
                else if (UK2Node_Event* TickEvent = Cast<UK2Node_Event>(Node))
                {
                    if (TickEvent->EventReference.GetMemberName() == TEXT("ReceiveTick"))
                        if (UEdGraphPin* Then = TickEvent->FindPin(TEXT("then"))) Then->BreakAllPinLinks();
                }
            }
            for (UEdGraphNode* Node : Remove) FBlueprintEditorUtils::RemoveNode(BP, Node, true);
        }
        FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
        FKismetEditorUtilities::CompileBlueprint(BP);
        if (BP->Status == BS_Error || !SavePatientAsset(BP)) return 1;
    }
    World->GetOutermost()->MarkPackageDirty();
    if (!UEditorLoadingAndSavingUtils::SaveMap(World, TEXT("/Game/Project/Maps/CNA_Map_01"))) return 1;
    UE_LOG(LogTemp, Display, TEXT("PatientCare: Dedicated joints, bed references, quiz isolation and manager references saved."));
    return 0;
}

