#include "CNAReliabilityCommandlet.h"
#include "Transfer/TransferManagerActor.h"
#include "Engine/DataTable.h"
#include <stdexcept>
#include "Engine/Blueprint.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_Variable.h"
#include "K2Node_EnhancedInputAction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Reliability/CNAReliabilityLibrary.h"
#include "Reliability/TransferHintMenuWidget.h"
#include "Reliability/VRWidgetInputComponent.h"
#include "Reliability/TrainingMediaControllerComponent.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_CallDelegate.h"
#include "K2Node_Self.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "FileHelpers.h"
#include "Patient/PatientActor.h"
#include "Transfer/BeltActor.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "InputTriggers.h"
#include "MediaPlayer.h"
#include "FileMediaSource.h"
#include "Components/WidgetInteractionComponent.h"
#include "Components/WidgetComponent.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"

UCNAReliabilityCommandlet::UCNAReliabilityCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

static FString DescribeBlueprint(UBlueprint* BP)
{
	FString Out = BP->GetPathName() + TEXT("\n");
	TArray<UEdGraph*> Graphs;
	BP->GetAllGraphs(Graphs);
	for (UEdGraph* Graph : Graphs)
	{
		Out += TEXT("GRAPH ") + Graph->GetName() + TEXT("\n");
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			Out += FString::Printf(TEXT(" NODE %s [%s] %s enabled=%d\n"), *Node->GetName(), *Node->GetClass()->GetName(), *Node->GetNodeTitle(ENodeTitleType::ListView).ToString(), (int)Node->GetDesiredEnabledState());
			for (UEdGraphPin* Pin : Node->Pins)
			{
				Out += FString::Printf(TEXT("  %s %s (%s) default=%s object=%s"), Pin->Direction == EGPD_Input ? TEXT("IN") : TEXT("OUT"), *Pin->PinName.ToString(), *Pin->PinType.PinCategory.ToString(), *Pin->DefaultValue, *GetPathNameSafe(Pin->DefaultObject));
				for (UEdGraphPin* Link : Pin->LinkedTo)
				{
					Out += TEXT(" -> ") + Link->GetOwningNode()->GetName() + TEXT(".") + Link->PinName.ToString();
				}
				Out += TEXT("\n");
			}
		}
	}
	if (BP->SimpleConstructionScript)
	{
		for (USCS_Node* Node : BP->SimpleConstructionScript->GetAllNodes())
		{
			Out += TEXT("COMPONENT ") + Node->GetVariableName().ToString() + TEXT(" ") + GetNameSafe(Node->ComponentClass) + TEXT("\n");
			for (TFieldIterator<FProperty> It(Node->ComponentTemplate->GetClass()); It; ++It)
			{
				FString Value;
				It->ExportText_InContainer(0, Value, Node->ComponentTemplate, Node->ComponentTemplate, Node->ComponentTemplate, PPF_None);
				if (Value.Len() < 500 && !Value.IsEmpty()) Out += It->GetName() + TEXT("=") + Value + TEXT("\n");
			}
		}
	}
	return Out;
}


namespace
{
void Require(bool Condition, const FString& Message)
{
    if (!Condition) { UE_LOG(LogTemp, Error, TEXT("CNA_RELIABILITY: %s"), *Message); throw std::runtime_error(TCHAR_TO_UTF8(*Message)); }
}

void SaveAsset(UObject* Asset)
{
    UPackage* Package = Asset->GetOutermost();
    Package->MarkPackageDirty();
    const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
    FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError;
    Require(UPackage::SavePackage(Package, Asset, *Filename, Args), TEXT("Could not save ") + Filename);
}

void CompileAndSave(UBlueprint* BP)
{
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
    FKismetEditorUtilities::CompileBlueprint(BP);
    Require(BP->Status != BS_Error, TEXT("Blueprint compile failed: ") + BP->GetName());
    SaveAsset(BP);
    UE_LOG(LogTemp, Display, TEXT("CNA_RELIABILITY: compiled and saved %s"), *BP->GetName());
}

UBlueprint* Blueprint(const TCHAR* Path)
{
    UBlueprint* BP = LoadObject<UBlueprint>(nullptr, Path);
    Require(BP != nullptr, FString(TEXT("Missing Blueprint ")) + Path);
    return BP;
}

UEdGraph* EventGraph(UBlueprint* BP)
{
    Require(BP->UbergraphPages.Num() > 0, BP->GetName() + TEXT(" has no event graph"));
    return BP->UbergraphPages[0];
}

UK2Node_CallFunction* LibraryCall(UEdGraph* Graph, FName Function, int32 X, int32 Y)
{
    UK2Node_CallFunction* Node = NewObject<UK2Node_CallFunction>(Graph);
    Node->FunctionReference.SetExternalMember(Function, UCNAReliabilityLibrary::StaticClass());
    Graph->AddNode(Node, false, false);
    Node->CreateNewGuid(); Node->PostPlacedNewNode(); Node->AllocateDefaultPins(); Node->NodePosX = X; Node->NodePosY = Y;
    return Node;
}

void Connect(UEdGraphPin* A, UEdGraphPin* B)
{
    Require(A && B, TEXT("Missing required graph pin"));
    Require(GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(A, B), TEXT("Could not connect ") + A->PinName.ToString() + TEXT(" to ") + B->PinName.ToString());
}

void ConnectSelf(UEdGraph* Graph, UK2Node_CallFunction* Call, FName Parameter)
{
    UK2Node_Self* Self = NewObject<UK2Node_Self>(Graph);
    Graph->AddNode(Self, false, false); Self->CreateNewGuid(); Self->PostPlacedNewNode(); Self->AllocateDefaultPins();
    Self->NodePosX = Call->NodePosX - 160; Self->NodePosY = Call->NodePosY + 160;
    Connect(Self->FindPin(UEdGraphSchema_K2::PN_Self), Call->FindPin(Parameter));
}

UK2Node_CustomEvent* CustomEvent(UEdGraph* Graph, FName Name)
{
    for (UEdGraphNode* Node : Graph->Nodes)
        if (UK2Node_CustomEvent* Event = Cast<UK2Node_CustomEvent>(Node)) if (Event->CustomFunctionName == Name) return Event;
    UK2Node_CustomEvent* Event = NewObject<UK2Node_CustomEvent>(Graph);
    Event->CustomFunctionName = Name;
    Graph->AddNode(Event, false, false); Event->CreateNewGuid(); Event->PostPlacedNewNode(); Event->AllocateDefaultPins();
    return Event;
}

void RerouteCustomEvent(UBlueprint* BP, FName EventName, FName Function)
{
    UEdGraph* Graph = EventGraph(BP);
    UK2Node_CustomEvent* Event = CustomEvent(Graph, EventName);
    Event->FindPin(UEdGraphSchema_K2::PN_Then)->BreakAllPinLinks();
    UK2Node_CallFunction* Call = LibraryCall(Graph, Function, Event->NodePosX + 300, Event->NodePosY);
    ConnectSelf(Graph, Call, TEXT("Display"));
    Connect(Event->FindPin(UEdGraphSchema_K2::PN_Then), Call->GetExecPin());
}

void AddComponent(UBlueprint* BP, UClass* Class, FName Name)
{
    Require(BP->SimpleConstructionScript != nullptr, TEXT("Missing SCS on ") + BP->GetName());
    for (USCS_Node* Node : BP->SimpleConstructionScript->GetAllNodes()) if (Node->ComponentClass == Class) return;
    BP->SimpleConstructionScript->AddNode(BP->SimpleConstructionScript->CreateNode(Class, Name));
}

void RemoveOldClickNodes(UBlueprint* BP)
{
    for (UEdGraph* Graph : BP->UbergraphPages)
    {
        TArray<UEdGraphNode*> Remove;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (UK2Node_EnhancedInputAction* Input = Cast<UK2Node_EnhancedInputAction>(Node))
                if (Input->InputAction && Input->InputAction->GetName().StartsWith(TEXT("IA_Menu_Interact"))) Remove.Add(Node);
            if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
                if (Call->FunctionReference.GetMemberName() == TEXT("PressPointerKey") || Call->FunctionReference.GetMemberName() == TEXT("ReleasePointerKey")) Remove.Add(Node);
        }
        for (UEdGraphNode* Node : Remove) FBlueprintEditorUtils::RemoveNode(BP, Node, true);
    }
}

UInputAction* MakeAction(const TCHAR* Name)
{
    const FString Path = FString(TEXT("/Game/VRTemplate/Input/Actions/")) + Name;
    UInputAction* Action = LoadObject<UInputAction>(nullptr, *Path);
    if (!Action)
    {
        Action = NewObject<UInputAction>(CreatePackage(*Path), Name, RF_Public | RF_Standalone);
        FAssetRegistryModule::AssetCreated(Action);
    }
    Action->ValueType = EInputActionValueType::Boolean;
    Action->bConsumeInput = true;
    Action->Triggers.Reset();
    UInputTriggerDown* Trigger = NewObject<UInputTriggerDown>(Action); Trigger->ActuationThreshold = 0.5f;
    Action->Triggers.Add(Trigger);
    SaveAsset(Action);
    return Action;
}

void MigrateInput()
{
    UInputAction* Left = MakeAction(TEXT("IA_UIInteract_Left"));
    UInputAction* Right = MakeAction(TEXT("IA_UIInteract_Right"));
    const TCHAR* Path = TEXT("/Game/VRTemplate/Input/IMC_UIInteract");
    UInputMappingContext* Context = LoadObject<UInputMappingContext>(nullptr, Path);
    if (!Context)
    {
        Context = NewObject<UInputMappingContext>(CreatePackage(Path), TEXT("IMC_UIInteract"), RF_Public | RF_Standalone);
        FAssetRegistryModule::AssetCreated(Context);
    }
    Context->UnmapAll();
    for (const TCHAR* Key : { TEXT("OculusTouch_Left_Trigger_Axis"), TEXT("ValveIndex_Left_Trigger_Axis"), TEXT("Vive_Left_Trigger_Axis"), TEXT("MixedReality_Left_Trigger_Axis") }) Context->MapKey(Left, FKey(Key));
    for (const TCHAR* Key : { TEXT("OculusTouch_Right_Trigger_Axis"), TEXT("ValveIndex_Right_Trigger_Axis"), TEXT("Vive_Right_Trigger_Axis"), TEXT("MixedReality_Right_Trigger_Axis") }) Context->MapKey(Right, FKey(Key));
    SaveAsset(Context);
    UInputMappingContext* MenuContext = LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/VRTemplate/Input/IMC_Menu"));
    Require(MenuContext != nullptr, TEXT("Missing menu context"));
    TSet<const UInputAction*> Old;
    for (const FEnhancedActionKeyMapping& Mapping : MenuContext->GetMappings()) if (Mapping.Action && Mapping.Action->GetName().StartsWith(TEXT("IA_Menu_Interact"))) Old.Add(Mapping.Action);
    for (const UInputAction* Action : Old) MenuContext->UnmapAllKeysFromAction(Action);
    SaveAsset(MenuContext);

    UBlueprint* Pawn = Blueprint(TEXT("/Game/VRTemplate/Blueprints/VRPawn"));
    UBlueprint* Menu = Blueprint(TEXT("/Game/VRTemplate/Blueprints/Menu"));
    RemoveOldClickNodes(Pawn); RemoveOldClickNodes(Menu);
    AddComponent(Pawn, UVRWidgetInputComponent::StaticClass(), TEXT("ReliableWidgetInput"));
    for (UBlueprint* BP : { Pawn, Menu })
    {
        for (USCS_Node* Node : BP->SimpleConstructionScript->GetAllNodes())
        {
            if (UWidgetInteractionComponent* WIC = Cast<UWidgetInteractionComponent>(Node->ComponentTemplate))
            {
                WIC->VirtualUserIndex = 0;
                WIC->PointerIndex = BP == Menu ? 2 : Node->GetVariableName().ToString().Contains(TEXT("Right")) ? 1 : 0;
            }
            if (UWidgetComponent* Widget = Cast<UWidgetComponent>(Node->ComponentTemplate))
            {
                Widget->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
                Widget->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
            }
        }
        CompileAndSave(BP);
    }

    UBlueprint* Widget = Blueprint(TEXT("/Game/VRTemplate/Blueprints/WidgetMenu"));
    UEdGraph* Graph = EventGraph(Widget);
    for (UEdGraphNode* Node : TArray<UEdGraphNode*>(Graph->Nodes))
    {
        UK2Node_ComponentBoundEvent* Event = Cast<UK2Node_ComponentBoundEvent>(Node);
        if (!Event) continue;
        FName Function = Event->ComponentPropertyName == TEXT("RestartButton") ? FName(TEXT("RestartTraining")) : Event->ComponentPropertyName == TEXT("ResetOrientationButton") ? FName(TEXT("ResetOrientation")) : Event->ComponentPropertyName == TEXT("ExitButton") ? FName(TEXT("ExitTraining")) : NAME_None;
        if (Function.IsNone()) continue;
        Event->FindPin(UEdGraphSchema_K2::PN_Then)->BreakAllPinLinks();
        UK2Node_CallFunction* Call = LibraryCall(Graph, Function, Event->NodePosX + 300, Event->NodePosY);
        ConnectSelf(Graph, Call, TEXT("WorldContextObject"));
        Connect(Event->FindPin(UEdGraphSchema_K2::PN_Then), Call->GetExecPin());
    }
    CompileAndSave(Widget);
}

void MigrateCursorMenu()
{
    UBlueprint* Menu = Blueprint(TEXT("/Game/VRTemplate/Blueprints/Menu"));
    UEdGraph* Graph = EventGraph(Menu);
    UK2Node_CallFunction* FaceCamera = nullptr;
    for (UEdGraphNode* Node : Graph->Nodes)
        if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
            if (Call->GetFName() == TEXT("K2Node_CallFunction_8") && Call->FunctionReference.GetMemberName() == TEXT("K2_SetWorldRotation")) FaceCamera = Call;
    Require(FaceCamera != nullptr, TEXT("Expected menu-facing rotation node missing"));
    // Keep head-facing positioning and opening/closing animation. Stop the
    // hybrid ray/cursor branch from moving the dot and reparenting pawn pointers.
    FaceCamera->GetThenPin()->BreakAllPinLinks();
    UK2Node_CustomEvent* Close = CustomEvent(Graph, TEXT("CloseMenu"));
    UEdGraphPin* Then = Close->FindPin(UEdGraphSchema_K2::PN_Then);
    bool bAlreadyPatched = false;
    for (UEdGraphPin* Link : Then->LinkedTo)
        if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Link->GetOwningNode()))
            bAlreadyPatched |= Call->FunctionReference.GetMemberName() == TEXT("StopMenuInteraction");
    if (!bAlreadyPatched)
    {
        TArray<UEdGraphPin*> Next = Then->LinkedTo;
        Then->BreakAllPinLinks();
        UK2Node_CallFunction* Release = LibraryCall(Graph, TEXT("StopMenuInteraction"), Close->NodePosX + 200, Close->NodePosY - 180);
        ConnectSelf(Graph, Release, TEXT("Menu"));
        Connect(Then, Release->GetExecPin());
        for (UEdGraphPin* Link : Next) Connect(Release->GetThenPin(), Link);
    }
    CompileAndSave(Menu);
}

void GuardPatientConversation(UBlueprint* BP, UEdGraphNode* Entry, bool bWidget)
{
    UEdGraph* Graph = Entry->GetGraph();
    UEdGraphPin* Then = Entry->FindPin(UEdGraphSchema_K2::PN_Then);
    Require(Then != nullptr, TEXT("Patient conversation entry has no execution output"));
    for (UEdGraphPin* Link : Then->LinkedTo)
        if (Link->GetOwningNode()->NodeComment == TEXT("PatientConversationPolicy")) return;
    TArray<UEdGraphPin*> Next = Then->LinkedTo;
    Then->BreakAllPinLinks();
    UK2Node_CallFunction* Apply = LibraryCall(Graph, TEXT("ApplyPatientConversationPolicy"), Entry->NodePosX + 220, Entry->NodePosY - 160);
    Apply->NodeComment = TEXT("PatientConversationPolicy");
    UK2Node_CallFunction* Enabled = LibraryCall(Graph, TEXT("IsPatientConversationEnabled"), Entry->NodePosX + 220, Entry->NodePosY - 300);
    UK2Node_IfThenElse* Gate = NewObject<UK2Node_IfThenElse>(Graph);
    Graph->AddNode(Gate, false, false); Gate->CreateNewGuid(); Gate->PostPlacedNewNode(); Gate->AllocateDefaultPins();
    Gate->NodePosX = Entry->NodePosX + 450; Gate->NodePosY = Entry->NodePosY;
    if (bWidget)
    {
        UK2Node_VariableGet* Owner = NewObject<UK2Node_VariableGet>(Graph);
        Owner->VariableReference.SetSelfMember(TEXT("OwningActor")); Graph->AddNode(Owner, false, false);
        Owner->CreateNewGuid(); Owner->PostPlacedNewNode(); Owner->AllocateDefaultPins();
        Connect(Owner->FindPin(TEXT("OwningActor")), Apply->FindPin(TEXT("ChatActor")));
        Connect(Owner->FindPin(TEXT("OwningActor")), Enabled->FindPin(TEXT("ChatActor")));
    }
    else
    {
        ConnectSelf(Graph, Apply, TEXT("ChatActor")); ConnectSelf(Graph, Enabled, TEXT("ChatActor"));
    }
    Connect(Then, Apply->GetExecPin()); Connect(Apply->GetThenPin(), Gate->GetExecPin());
    Connect(Enabled->FindPin(UEdGraphSchema_K2::PN_ReturnValue), Gate->GetConditionPin());
    for (UEdGraphPin* Link : Next) Connect(Gate->GetThenPin(), Link);
}

void DisablePatientConversation()
{
    const FString BackupDir = FPaths::ProjectSavedDir() / TEXT("InteractionLocks/BeforeChanges-2026-10-06/Content");
    for (const TCHAR* Path : { TEXT("/Game/Project/Blueprints/Actors/BP_PatientChat"), TEXT("/Game/Project/Blueprints/UI/WBP_PatientChat"), TEXT("/Game/Project/Maps/CNA_Map_01") })
    {
        const bool bMap = FString(Path).EndsWith(TEXT("CNA_Map_01"));
        const FString Ext = bMap ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension();
        const FString Source = FPackageName::LongPackageNameToFilename(Path, Ext);
        const FString Backup = BackupDir / (FString(Path).RightChop(6) + Ext);
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Backup), true);
        if (!IFileManager::Get().FileExists(*Backup)) Require(IFileManager::Get().Copy(*Backup, *Source) == COPY_OK, TEXT("Patient conversation backup failed"));
    }
    UBlueprint* Chat = Blueprint(TEXT("/Game/Project/Blueprints/Actors/BP_PatientChat"));
    const FName FlagName(TEXT("bEnablePatientConversation"));
    if (!FindFProperty<FBoolProperty>(Chat->GeneratedClass, FlagName))
    {
        FEdGraphPinType Type; Type.PinCategory = UEdGraphSchema_K2::PC_Boolean;
        Require(FBlueprintEditorUtils::AddMemberVariable(Chat, FlagName, Type, TEXT("true")), TEXT("Could not add conversation enable flag"));
        for (FBPVariableDescription& Variable : Chat->NewVariables)
            if (Variable.VarName == FlagName) Variable.PropertyFlags = CPF_Edit | CPF_BlueprintVisible;
    }
    TArray<UEdGraph*> Graphs; Chat->GetAllGraphs(Graphs);
    for (UEdGraph* Graph : Graphs)
    {
        TArray<UEdGraphNode*> Entries;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (UK2Node_CustomEvent* Event = Cast<UK2Node_CustomEvent>(Node))
                if (Event->CustomFunctionName == TEXT("EnableChatPanel")) Entries.Add(Node);
            if (Cast<UK2Node_FunctionEntry>(Node) && Graph->GetName() == TEXT("EnableChatPanel")) Entries.Add(Node);
        }
        for (UEdGraphNode* Entry : Entries) GuardPatientConversation(Chat, Entry, false);
        // Retain widget creation and OwningActor assignment on BeginPlay, then apply the flag.
        const auto ExistingNodes = Graph->Nodes;
        for (UEdGraphNode* Node : ExistingNodes)
        {
            UK2Node_VariableSet* Set = Cast<UK2Node_VariableSet>(Node);
            if (!Set || Set->VariableReference.GetMemberName() != TEXT("OwningActor")) continue;
            UEdGraphPin* Then = Set->FindPin(UEdGraphSchema_K2::PN_Then);
            bool bGuarded = false;
            for (UEdGraphPin* Link : Then->LinkedTo) bGuarded |= Link->GetOwningNode()->NodeComment == TEXT("PatientConversationPolicy");
            if (bGuarded) continue;
            TArray<UEdGraphPin*> Next = Then->LinkedTo; Then->BreakAllPinLinks();
            UK2Node_CallFunction* Apply = LibraryCall(Graph, TEXT("ApplyPatientConversationPolicy"), Node->NodePosX + 240, Node->NodePosY);
            Apply->NodeComment = TEXT("PatientConversationPolicy"); ConnectSelf(Graph, Apply, TEXT("ChatActor"));
            Connect(Then, Apply->GetExecPin()); for (UEdGraphPin* Link : Next) Connect(Apply->GetThenPin(), Link);
        }
    }
    CompileAndSave(Chat);
    UBlueprint* Widget = Blueprint(TEXT("/Game/Project/Blueprints/UI/WBP_PatientChat"));
    Graphs.Reset(); Widget->GetAllGraphs(Graphs);
    for (UEdGraph* Graph : Graphs)
    {
        TArray<UEdGraphNode*> Entries;
        for (UEdGraphNode* Node : Graph->Nodes)
            if (UK2Node_ComponentBoundEvent* Event = Cast<UK2Node_ComponentBoundEvent>(Node))
                if (Event->DelegatePropertyName == TEXT("OnClicked")) Entries.Add(Node);
        for (UEdGraphNode* Entry : Entries) GuardPatientConversation(Widget, Entry, true);
    }
    CompileAndSave(Widget);
    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(TEXT("/Game/Project/Maps/CNA_Map_01"));
    Require(World != nullptr, TEXT("Could not load training map"));
    int32 Count = 0;
    for (AActor* Actor : World->PersistentLevel->Actors)
    {
        if (!Actor || Actor->GetClass() != Chat->GeneratedClass) continue;
        FBoolProperty* Flag = FindFProperty<FBoolProperty>(Actor->GetClass(), FlagName);
        Require(Flag != nullptr, TEXT("Conversation flag missing")); Flag->SetPropertyValue_InContainer(Actor, false);
        UCNAReliabilityLibrary::ApplyPatientConversationPolicy(Actor); ++Count;
    }
    Require(Count == 1, TEXT("Expected one placed patient conversation actor"));
    Require(UEditorLoadingAndSavingUtils::SaveMap(World, TEXT("/Game/Project/Maps/CNA_Map_01")), TEXT("Could not save patient UI override"));
    UE_LOG(LogTemp, Display, TEXT("CNA_PATIENT_UI: conversation disabled; actor/widget references retained"));
}

void EnableTrainingUI()
{
    const FString BackupDir = FPaths::ProjectSavedDir() / TEXT("UITesting/BeforeEnable");
    for (const TCHAR* Path : { TEXT("/Game/Project/Blueprints/Actors/BP_PatientChat"), TEXT("/Game/Project/Maps/CNA_Map_01") })
    {
        const FString Ext = FString(Path).EndsWith(TEXT("CNA_Map_01")) ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension();
        const FString Source = FPackageName::LongPackageNameToFilename(Path, Ext);
        const FString Backup = BackupDir / (FString(Path).RightChop(6) + Ext);
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Backup), true);
        if (!IFileManager::Get().FileExists(*Backup)) Require(IFileManager::Get().Copy(*Backup, *Source) == COPY_OK, TEXT("UI backup failed"));
    }
    UBlueprint* Chat = Blueprint(TEXT("/Game/Project/Blueprints/Actors/BP_PatientChat"));
    const FName PreviewFlag(TEXT("bShowConversationForUITesting"));
    if (!FindFProperty<FBoolProperty>(Chat->GeneratedClass, PreviewFlag))
    {
        FEdGraphPinType Type; Type.PinCategory = UEdGraphSchema_K2::PC_Boolean;
        Require(FBlueprintEditorUtils::AddMemberVariable(Chat, PreviewFlag, Type, TEXT("false")), TEXT("Could not add UI testing flag"));
        for (FBPVariableDescription& Variable : Chat->NewVariables)
            if (Variable.VarName == PreviewFlag) { Variable.PropertyFlags = CPF_Edit | CPF_BlueprintVisible; Variable.Category = FText::FromString(TEXT("UI Testing")); }
        FKismetEditorUtilities::CompileBlueprint(Chat);
        Require(Chat->Status != BS_Error, TEXT("Could not compile UI testing flag"));
    }
    UEdGraph* Graph = EventGraph(Chat);
    bool bHasPreview = false;
    UK2Node_CallFunction* StartupPolicy = nullptr;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        bHasPreview |= Node->NodeComment == TEXT("ShowConversationForUITesting");
        if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
            if (Call->FunctionReference.GetMemberName() == TEXT("ApplyPatientConversationPolicy"))
                for (UEdGraphPin* Link : Call->GetExecPin()->LinkedTo)
                    if (UK2Node_VariableSet* Set = Cast<UK2Node_VariableSet>(Link->GetOwningNode()))
                        if (Set->VariableReference.GetMemberName() == TEXT("OwningActor")) StartupPolicy = Call;
    }
    Require(StartupPolicy != nullptr, TEXT("Conversation widget initialization policy missing"));
    if (!bHasPreview)
    {
        Require(StartupPolicy->GetThenPin()->LinkedTo.IsEmpty(), TEXT("Conversation startup unexpectedly has downstream logic"));
        UK2Node_VariableGet* Flag = NewObject<UK2Node_VariableGet>(Graph);
        Flag->VariableReference.SetSelfMember(PreviewFlag);
        Graph->AddNode(Flag, false, false); Flag->CreateNewGuid(); Flag->PostPlacedNewNode(); Flag->AllocateDefaultPins();
        Flag->NodePosX = StartupPolicy->NodePosX + 220; Flag->NodePosY = StartupPolicy->NodePosY + 160;
        UK2Node_IfThenElse* Gate = NewObject<UK2Node_IfThenElse>(Graph);
        Graph->AddNode(Gate, false, false); Gate->CreateNewGuid(); Gate->PostPlacedNewNode(); Gate->AllocateDefaultPins();
        Gate->NodeComment = TEXT("ShowConversationForUITesting"); Gate->NodePosX = StartupPolicy->NodePosX + 240; Gate->NodePosY = StartupPolicy->NodePosY;
        UK2Node_CallFunction* Show = NewObject<UK2Node_CallFunction>(Graph);
        Show->FunctionReference.SetSelfMember(TEXT("EnableChatPanel"));
        Graph->AddNode(Show, false, false); Show->CreateNewGuid(); Show->PostPlacedNewNode(); Show->AllocateDefaultPins();
        Show->NodePosX = Gate->NodePosX + 240; Show->NodePosY = Gate->NodePosY;
        Connect(StartupPolicy->GetThenPin(), Gate->GetExecPin()); Connect(Flag->FindPin(PreviewFlag), Gate->GetConditionPin()); Connect(Gate->GetThenPin(), Show->GetExecPin());
    }
    CompileAndSave(Chat);
    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(TEXT("/Game/Project/Maps/CNA_Map_01"));
    Require(World != nullptr, TEXT("Could not load UI testing map"));
    int32 Count = 0;
    for (AActor* Actor : World->PersistentLevel->Actors)
    {
        if (!Actor || Actor->GetClass() != Chat->GeneratedClass) continue;
        for (FName Name : { FName(TEXT("bEnablePatientConversation")), PreviewFlag })
        {
            FBoolProperty* Flag = FindFProperty<FBoolProperty>(Actor->GetClass(), Name);
            Require(Flag != nullptr, TEXT("Conversation flag missing")); Flag->SetPropertyValue_InContainer(Actor, true);
        }
        UCNAReliabilityLibrary::ApplyPatientConversationPolicy(Actor);
        UWidgetComponent* Panel = Cast<UWidgetComponent>(UCNAReliabilityLibrary::ObjectProperty(Actor, TEXT("ChatPanel")));
        Require(Panel != nullptr, TEXT("Conversation panel missing"));
        Panel->SetVisibility(true); Panel->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
        ++Count;
    }
    Require(Count == 1, TEXT("Expected one patient conversation actor"));
    Require(UEditorLoadingAndSavingUtils::SaveMap(World, TEXT("/Game/Project/Maps/CNA_Map_01")), TEXT("Could not save enabled UI"));
    UE_LOG(LogTemp, Display, TEXT("CNA_UI_TESTING: conversation enabled and shown after widget initialization; existing stage-driven widgets retained"));
}

void MigrateMedia()
{
    UBlueprint* Display = Blueprint(TEXT("/Game/Project/Blueprints/Actors/BP_MediaDisplay"));
    UEdGraph* Graph = EventGraph(Display);
    UK2Node_CallDelegate* Complete = nullptr;
    TArray<UEdGraphNode*> Remove;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (UK2Node_CallDelegate* Call = Cast<UK2Node_CallDelegate>(Node)) if (Call->GetPropertyName() == TEXT("OnPlaybackStopped")) Complete = Call;
        if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
            if (Call->FunctionReference.GetMemberName() == TEXT("OpenSource") || Call->FunctionReference.GetMemberName() == TEXT("Close")) Remove.Add(Node);
        // Display actors must never subscribe directly to the shared player's completion event.
        if (Node->GetClass()->GetName() == TEXT("K2Node_AddDelegate") && Node->GetNodeTitle(ENodeTitleType::ListView).ToString().Contains(TEXT("On End Reached"))) Remove.Add(Node);
    }
    Require(Complete != nullptr, TEXT("Missing OnPlaybackStopped dispatcher in media display"));
    Complete->GetExecPin()->BreakAllPinLinks();
    UK2Node_CustomEvent* Notify = CustomEvent(Graph, TEXT("NotifyPlaybackComplete"));
    Notify->FindPin(UEdGraphSchema_K2::PN_Then)->BreakAllPinLinks();
    Connect(Notify->FindPin(UEdGraphSchema_K2::PN_Then), Complete->GetExecPin());
    RerouteCustomEvent(Display, TEXT("StartPlayback"), TEXT("RequestPlayback"));
    RerouteCustomEvent(Display, TEXT("StopPlayback"), TEXT("CancelPlayback"));
    for (UEdGraphNode* Node : Remove) FBlueprintEditorUtils::RemoveNode(Display, Node, true);
    for (USCS_Node* Node : Display->SimpleConstructionScript->GetAllNodes())
        if (UWidgetComponent* Widget = Cast<UWidgetComponent>(Node->ComponentTemplate))
        {
            Widget->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            Widget->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
        }
    CompileAndSave(Display);

    UBlueprint* Handler = Blueprint(TEXT("/Game/Project/Blueprints/Actors/BP_MediaEventHandler"));
    AddComponent(Handler, UTrainingMediaControllerComponent::StaticClass(), TEXT("ReliableMediaController"));
    UEdGraph* HandlerGraph = EventGraph(Handler);
    for (FName Name : { FName(TEXT("RequestPlayback")), FName(TEXT("CancelPlayback")), FName(TEXT("RetryPlayback")) })
    {
        UK2Node_CustomEvent* Event = CustomEvent(HandlerGraph, Name);
        Event->FindPin(UEdGraphSchema_K2::PN_Then)->BreakAllPinLinks();
        UK2Node_CallFunction* Call = LibraryCall(HandlerGraph, Name, Event->NodePosX + 300, Event->NodePosY);
        if (Name != TEXT("RetryPlayback"))
        {
            UEdGraphPin* Parameter = Event->FindPin(TEXT("Display"));
            if (!Parameter)
            {
                FEdGraphPinType Type; Type.PinCategory = UEdGraphSchema_K2::PC_Object; Type.PinSubCategoryObject = AActor::StaticClass();
                Parameter = Event->CreateUserDefinedPin(TEXT("Display"), Type, EGPD_Output);
            }
            Connect(Parameter, Call->FindPin(TEXT("Display")));
        }
        else ConnectSelf(HandlerGraph, Call, TEXT("WorldContextObject"));
        Connect(Event->FindPin(UEdGraphSchema_K2::PN_Then), Call->GetExecPin());
    }
    CompileAndSave(Handler);
    UMediaPlayer* Player = LoadObject<UMediaPlayer>(nullptr, TEXT("/Game/Project/Media/MP_Display"));
    Require(Player != nullptr, TEXT("Missing shared media player")); Player->PlayOnOpen = false; SaveAsset(Player);
}

void GateTrainingExec(UBlueprint* BP, UEdGraphPin* Source, FName Function)
{
    Require(Source && Source->Direction == EGPD_Output, TEXT("Missing training exec source"));
    if (Source->LinkedTo.Num() && Source->LinkedTo[0]->GetOwningNode()->NodeComment == TEXT("CNA_GuidedGate")) return;
    UEdGraph* Graph = Source->GetOwningNode()->GetGraph();
    const TArray<UEdGraphPin*> Previous = Source->LinkedTo;
    Source->BreakAllPinLinks();
    UK2Node_CallFunction* Call = LibraryCall(Graph, Function, Source->GetOwningNode()->NodePosX + 180, Source->GetOwningNode()->NodePosY + 80);
    Call->NodeComment = TEXT("CNA_GuidedGate");
    ConnectSelf(Graph, Call, TEXT("WorldContextObject"));
    UK2Node_IfThenElse* Gate = NewObject<UK2Node_IfThenElse>(Graph);
    Graph->AddNode(Gate, false, false); Gate->CreateNewGuid(); Gate->PostPlacedNewNode(); Gate->AllocateDefaultPins();
    Gate->NodeComment = TEXT("CNA_GuidedGate");
    Gate->NodePosX = Call->NodePosX + 220; Gate->NodePosY = Call->NodePosY;
    if (Call->GetExecPin()) { Connect(Source, Call->GetExecPin()); Connect(Call->GetThenPin(), Gate->GetExecPin()); }
    else Connect(Source, Gate->GetExecPin());
    Connect(Call->FindPin(UEdGraphSchema_K2::PN_ReturnValue), Gate->GetConditionPin());
    for (UEdGraphPin* Target : Previous) Connect(Gate->GetThenPin(), Target);
}

void ReplaceTrainingEvent(UBlueprint* BP, UEdGraphNode* Event, FName Function, FName Context)
{
    UEdGraphPin* Source = Event->FindPin(UEdGraphSchema_K2::PN_Then);
    Source->BreakAllPinLinks();
    UK2Node_CallFunction* Call = LibraryCall(Event->GetGraph(), Function, Event->NodePosX + 260, Event->NodePosY);
    Call->NodeComment = TEXT("CNA_GuidedCallback");
    ConnectSelf(Event->GetGraph(), Call, Context);
    Connect(Source, Call->GetExecPin());
}

void MigrateGuidedTraining()
{
    UBlueprint* Quiz = Blueprint(TEXT("/Game/Project/Blueprints/UI/WBP_QuizPanel"));
    for (UEdGraphNode* Node : TArray<TObjectPtr<UEdGraphNode>>(EventGraph(Quiz)->Nodes))
    {
        if (UK2Node_ComponentBoundEvent* Event = Cast<UK2Node_ComponentBoundEvent>(Node))
        {
            int32 Answer = Event->ComponentPropertyName == TEXT("Option1Button") ? 1 : Event->ComponentPropertyName == TEXT("Option2Button") ? 2 : Event->ComponentPropertyName == TEXT("Option3Button") ? 3 : 0;
            if (!Answer) continue;
            ReplaceTrainingEvent(Quiz, Event, TEXT("SubmitGuidedQuiz"), TEXT("Widget"));
            UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Event->FindPin(UEdGraphSchema_K2::PN_Then)->LinkedTo[0]->GetOwningNode());
            Call->FindPin(TEXT("Answer"))->DefaultValue = FString::FromInt(Answer);
        }
    }
    for (UEdGraph* Graph : Quiz->FunctionGraphs)
        if (Graph->GetFName() == TEXT("MakePlayerTaskAfterQuizCompleted"))
            for (UEdGraphNode* Node : TArray<TObjectPtr<UEdGraphNode>>(Graph->Nodes))
                if (UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node)) Entry->FindPin(UEdGraphSchema_K2::PN_Then)->BreakAllPinLinks();
    // Both training teleports use the already-proven camera-sphere pawn fade.
    for (UEdGraphNode* Node : TArray<TObjectPtr<UEdGraphNode>>(EventGraph(Quiz)->Nodes))
    {
        UK2Node_CallFunction* Old = Cast<UK2Node_CallFunction>(Node);
        if (!Old || Old->FunctionReference.GetMemberName() != TEXT("StartCameraFade")) continue;
        const bool bIn = FCString::Atof(*Old->FindPin(TEXT("FromAlpha"))->DefaultValue) > .5f;
        UK2Node_CallFunction* Call = LibraryCall(Old->GetGraph(), bIn ? TEXT("TrainingFadeIn") : TEXT("TrainingFadeOut"), Old->NodePosX, Old->NodePosY);
        ConnectSelf(Old->GetGraph(), Call, TEXT("WorldContextObject"));
        const auto Inputs = Old->GetExecPin()->LinkedTo; const auto Outputs = Old->GetThenPin()->LinkedTo;
        Old->GetExecPin()->BreakAllPinLinks(); Old->GetThenPin()->BreakAllPinLinks();
        for (UEdGraphPin* Pin : Inputs) Connect(Pin, Call->GetExecPin());
        for (UEdGraphPin* Pin : Outputs) Connect(Call->GetThenPin(), Pin);
        FBlueprintEditorUtils::RemoveNode(Quiz, Old, true);
    }
    CompileAndSave(Quiz);

    UBlueprint* Wrong = Blueprint(TEXT("/Game/Project/Blueprints/UI/WBP_WrongAnswerPopup"));
    for (UEdGraphNode* Node : TArray<TObjectPtr<UEdGraphNode>>(EventGraph(Wrong)->Nodes))
        if (UK2Node_ComponentBoundEvent* Event = Cast<UK2Node_ComponentBoundEvent>(Node))
            if (Event->ComponentPropertyName == TEXT("OKButton")) ReplaceTrainingEvent(Wrong, Event, TEXT("DismissGuidedWrongAnswer"), TEXT("WorldContextObject"));
    CompileAndSave(Wrong);

    UBlueprint* Chat = Blueprint(TEXT("/Game/Project/Blueprints/UI/WBP_PatientChat"));
    for (UEdGraphNode* Node : TArray<TObjectPtr<UEdGraphNode>>(EventGraph(Chat)->Nodes))
    {
        if (UK2Node_ComponentBoundEvent* Event = Cast<UK2Node_ComponentBoundEvent>(Node))
        {
            if (Event->ComponentPropertyName == TEXT("NextButton")) ReplaceTrainingEvent(Chat, Event, TEXT("BeginGuidedTransferLesson"), TEXT("WorldContextObject"));
            if (Event->ComponentPropertyName == TEXT("PatientInteractButton")) GateTrainingExec(Chat, Event->FindPin(UEdGraphSchema_K2::PN_Then), TEXT("BeginGuidedConversationResponse"));
        }
        if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
        {
            if (Call->FunctionReference.GetMemberName() != TEXT("StartPlayback")) continue;
            const auto Inputs = Call->GetExecPin()->LinkedTo; const auto Outputs = Call->GetThenPin()->LinkedTo;
            Call->GetExecPin()->BreakAllPinLinks(); Call->GetThenPin()->BreakAllPinLinks();
            for (UEdGraphPin* Input : Inputs) for (UEdGraphPin* Output : Outputs) Connect(Input, Output);
        }
    }
    CompileAndSave(Chat);

    UBlueprint* ChatActor = Blueprint(TEXT("/Game/Project/Blueprints/Actors/BP_PatientChat"));
    ReplaceTrainingEvent(ChatActor, CustomEvent(EventGraph(ChatActor), TEXT("EnableChatPanel")), TEXT("RequestGuidedConversationPanel"), TEXT("ChatActor"));
    CompileAndSave(ChatActor);

    UBlueprint* Handler = Blueprint(TEXT("/Game/Project/Blueprints/Actors/BP_MediaEventHandler"));
    GateTrainingExec(Handler, CustomEvent(EventGraph(Handler), TEXT("OnContinueClicked_Event"))->FindPin(UEdGraphSchema_K2::PN_Then), TEXT("BeginGuidedRoomTransition"));
    for (UEdGraphNode* Node : TArray<TObjectPtr<UEdGraphNode>>(EventGraph(Handler)->Nodes))
    {
        UK2Node_CallFunction* Old = Cast<UK2Node_CallFunction>(Node);
        if (!Old || Old->FunctionReference.GetMemberName() != TEXT("StartCameraFade")) continue;
        const bool bIn = FCString::Atof(*Old->FindPin(TEXT("FromAlpha"))->DefaultValue) > .5f;
        UK2Node_CallFunction* Call = LibraryCall(Old->GetGraph(), bIn ? TEXT("TrainingFadeIn") : TEXT("TrainingFadeOut"), Old->NodePosX, Old->NodePosY);
        ConnectSelf(Old->GetGraph(), Call, TEXT("WorldContextObject"));
        const auto Inputs = Old->GetExecPin()->LinkedTo; const auto Outputs = Old->GetThenPin()->LinkedTo;
        Old->GetExecPin()->BreakAllPinLinks(); Old->GetThenPin()->BreakAllPinLinks();
        for (UEdGraphPin* Pin : Inputs) Connect(Pin, Call->GetExecPin());
        UEdGraphPin* Then = Call->GetThenPin();
        if (bIn)
        {
            UK2Node_CallFunction* Finished = LibraryCall(Old->GetGraph(), TEXT("FinishGuidedRoomTransition"), Old->NodePosX + 250, Old->NodePosY);
            ConnectSelf(Old->GetGraph(), Finished, TEXT("WorldContextObject"));
            Connect(Then, Finished->GetExecPin()); Then = Finished->GetThenPin();
        }
        for (UEdGraphPin* Pin : Outputs) Connect(Then, Pin);
        FBlueprintEditorUtils::RemoveNode(Handler, Old, true);
    }
    CompileAndSave(Handler);

    UBlueprint* Pawn = Blueprint(TEXT("/Game/VRTemplate/Blueprints/VRPawn"));
    TArray<UEdGraph*> Graphs; Pawn->GetAllGraphs(Graphs);
    for (UEdGraph* Graph : Graphs)
    {
        for (UEdGraphNode* Node : TArray<TObjectPtr<UEdGraphNode>>(Graph->Nodes))
        {
            if (UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node))
                if (Graph->GetFName() == TEXT("StartTeleportTrace") || Graph->GetFName() == TEXT("TeleportTrace") || Graph->GetFName() == TEXT("TryTeleport") || Graph->GetFName() == TEXT("SnapTurn"))
                    GateTrainingExec(Pawn, Entry->FindPin(UEdGraphSchema_K2::PN_Then), TEXT("IsTrainingMovementAllowed"));
            if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
            {
                const FName Name = Call->FunctionReference.GetMemberName();
                if (Name != TEXT("AddMovementInput") && Name != TEXT("K2_AddActorWorldOffset") && Name != TEXT("AddControllerYawInput")) continue;
                if (!Call->GetExecPin()) continue;
                const auto Inputs = Call->GetExecPin()->LinkedTo;
                for (UEdGraphPin* Input : Inputs) GateTrainingExec(Pawn, Input, TEXT("IsTrainingMovementAllowed"));
            }
        }
    }
    CompileAndSave(Pawn);

    // Retire active legacy chair/lift commands without removing their assets.
    UBlueprint* Step = Blueprint(TEXT("/Game/Project/Blueprints/Actors/BP_StepTrigger"));
    for (UEdGraphNode* Node : EventGraph(Step)->Nodes)
        if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
            if (Call->FunctionReference.GetMemberName() == TEXT("TransitionToStandingPose") || Call->FunctionReference.GetMemberName() == TEXT("TransitionToWheelChairSitting")) Call->GetExecPin()->BreakAllPinLinks();
    CompileAndSave(Step);

    UDataTable* Questions = LoadObject<UDataTable>(nullptr, TEXT("/Game/Project/Blueprints/Data/Datables/DT_Questions"));
    Require(Questions && Questions->GetRowMap().Contains(TEXT("1")), TEXT("Missing patient-room quiz row"));
    bool bUpdated = false;
    for (TFieldIterator<FProperty> It(Questions->GetRowStruct()); It; ++It)
        if (It->GetName().StartsWith(TEXT("OptionB")))
            if (FStrProperty* Field = CastField<FStrProperty>(*It))
            { Field->SetPropertyValue_InContainer(Questions->GetRowMap()[TEXT("1")], TEXT("Help the patient sit with head/neck support")); bUpdated = true; }
    Require(bUpdated, TEXT("Could not update verified OptionB quiz field"));
    SaveAsset(Questions);

    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(TEXT("/Game/Project/Maps/CNA_Map_01"));
    Require(World != nullptr, TEXT("Could not load guided training map"));
    int32 Managers = 0, Chats = 0;
    for (ULevel* Level : World->GetLevels()) for (AActor* Actor : Level->Actors)
    {
        if (ATransferManagerActor* Manager = Cast<ATransferManagerActor>(Actor)) { Manager->bGuidedTraining = true; ++Managers; }
        if (Actor && Actor->GetClass()->GetFName() == TEXT("BP_PatientChat_C"))
        {
            for (const TPair<FName, bool>& Flag : { TPair<FName, bool>(TEXT("bEnablePatientConversation"), true), TPair<FName, bool>(TEXT("bShowConversationForUITesting"), false) })
            {
                FBoolProperty* Field = FindFProperty<FBoolProperty>(Actor->GetClass(), Flag.Key);
                Require(Field != nullptr, TEXT("Missing conversation configuration flag"));
                Field->SetPropertyValue_InContainer(Actor, Flag.Value);
            }
            ++Chats;
        }
    }
    Require(Managers == 1 && Chats == 1, TEXT("Expected one native manager and conversation actor"));
    Require(UEditorLoadingAndSavingUtils::SaveMap(World, TEXT("/Game/Project/Maps/CNA_Map_01")), TEXT("Could not save guided training map"));
    UE_LOG(LogTemp, Display, TEXT("CNA_GUIDED: migration complete; native guided handoff and delivery flags saved"));
}

void ApplyQuestFeedback()
{
    UBlueprint* Pawn = Blueprint(TEXT("/Game/VRTemplate/Blueprints/VRPawn"));
    TArray<UEdGraph*> Graphs; Pawn->GetAllGraphs(Graphs);
    int32 MovementCalls = 0, CommitGates = 0, TraceGates = 0, TrackingFixes = 0;
    for (UEdGraph* Graph : Graphs)
    {
        for (UEdGraphNode* Node : TArray<TObjectPtr<UEdGraphNode>>(Graph->Nodes))
        {
            if (UK2Node_CallFunction* Old = Cast<UK2Node_CallFunction>(Node))
            {
                const FName Name = Old->FunctionReference.GetMemberName();
                if (Graph->GetFName() == TEXT("TryTeleport") && Name == TEXT("IsTrainingMovementAllowed"))
                {
                    Old->FunctionReference.SetExternalMember(TEXT("IsPendingTrainingTeleportAllowed"), UCNAReliabilityLibrary::StaticClass());
                    Old->ReconstructNode(); ++CommitGates;
                }
                if (Name == TEXT("AddMovementInput"))
                {
                    UK2Node_CallFunction* Call = LibraryCall(Graph, TEXT("AddTrainingMovementInput"), Old->NodePosX, Old->NodePosY);
                    Call->NodeComment = TEXT("CNA_TrainingRadius");
                    ConnectSelf(Graph, Call, TEXT("Pawn"));
                    for (FName PinName : { UEdGraphSchema_K2::PN_Execute, UEdGraphSchema_K2::PN_Then, FName(TEXT("WorldDirection")), FName(TEXT("ScaleValue")), FName(TEXT("bForce")) })
                    {
                        UEdGraphPin* Source = Old->FindPin(PinName); UEdGraphPin* Target = Call->FindPin(PinName);
                        Require(Source && Target, TEXT("Movement adapter pin missing"));
                        Target->DefaultValue = Source->DefaultValue;
                        const auto Links = Source->LinkedTo; Source->BreakAllPinLinks();
                        for (UEdGraphPin* Link : Links)
                            if (Target->Direction == EGPD_Input) Connect(Link, Target); else Connect(Target, Link);
                    }
                    FBlueprintEditorUtils::RemoveNode(Pawn, Old, true); ++MovementCalls;
                }
                // This offset recentres the collision root around physical HMD tracking. It must always run.
                if (Name == TEXT("K2_AddActorLocalOffset"))
                {
                    const auto Links = Old->GetExecPin()->LinkedTo;
                    for (UEdGraphPin* Link : Links)
                    {
                        UK2Node_IfThenElse* Gate = Cast<UK2Node_IfThenElse>(Link->GetOwningNode());
                        if (!Gate || Gate->NodeComment != TEXT("CNA_GuidedGate")) continue;
                        const auto Inputs = Gate->GetExecPin()->LinkedTo;
                        Gate->GetExecPin()->BreakAllPinLinks(); Old->GetExecPin()->BreakAllPinLinks();
                        for (UEdGraphPin* Input : Inputs) Connect(Input, Old->GetExecPin());
                        ++TrackingFixes;
                    }
                }
            }
            if (Graph->GetFName() == TEXT("IsValidTeleportLocation"))
            {
                UK2Node_FunctionResult* Result = Cast<UK2Node_FunctionResult>(Node);
                if (!Result) continue;
                UEdGraphPin* Valid = Result->FindPin(UEdGraphSchema_K2::PN_ReturnValue);
                UEdGraphPin* Location = Result->FindPin(TEXT("ProjectedLocation"));
                Require(Valid && Location, TEXT("Teleport validity result missing"));
                if (Valid->LinkedTo.Num() && Valid->LinkedTo[0]->GetOwningNode()->NodeComment == TEXT("CNA_TrainingRadius")) continue;
                UK2Node_CallFunction* Check = LibraryCall(Graph, TEXT("ValidateTrainingTeleportLocation"), Result->NodePosX - 300, Result->NodePosY + 100);
                Check->NodeComment = TEXT("CNA_TrainingRadius"); ConnectSelf(Graph, Check, TEXT("WorldContextObject"));
                Check->FindPin(TEXT("bNavigationValid"))->DefaultValue = Valid->DefaultValue;
                const auto Previous = Valid->LinkedTo; Valid->BreakAllPinLinks();
                for (UEdGraphPin* Link : Previous) Connect(Link, Check->FindPin(TEXT("bNavigationValid")));
                for (UEdGraphPin* Link : Location->LinkedTo) Connect(Link, Check->FindPin(TEXT("ProjectedLocation")));
                Connect(Check->FindPin(UEdGraphSchema_K2::PN_ReturnValue), Valid); ++TraceGates;
            }
        }
    }
    CompileAndSave(Pawn);
    UWidgetBlueprint* Menu = Cast<UWidgetBlueprint>(Blueprint(TEXT("/Game/VRTemplate/Blueprints/WidgetMenu")));
    UButton* Exit = Menu && Menu->WidgetTree ? Cast<UButton>(Menu->WidgetTree->FindWidget(TEXT("ExitButton"))) : nullptr;
    UTextBlock* Label = Exit ? Cast<UTextBlock>(Exit->GetContent()) : nullptr;
    Require(Label != nullptr, TEXT("Existing exit-button label missing"));
    Label->SetText(FText::FromString(TEXT("Quit"))); CompileAndSave(Menu);
    UE_LOG(LogTemp, Display, TEXT("CNA_QUEST_FEEDBACK: radius adapters=%d commit=%d trace=%d tracking=%d; Quit label saved"), MovementCalls, CommitGates, TraceGates, TrackingFixes);
}

void FixPopupLayouts()
{
    for (const TCHAR* Name : { TEXT("WBP_MediaText"), TEXT("WBP_WrongAnswerPopup"), TEXT("WBP_CompletionPanel") })
    {
        UWidgetBlueprint* BP = Cast<UWidgetBlueprint>(Blueprint(*(FString(TEXT("/Game/Project/Blueprints/UI/")) + Name)));
        Require(BP && BP->WidgetTree, TEXT("Popup widget tree missing"));
        const bool bInstruction = FString(Name) == TEXT("WBP_MediaText");
        const bool bWrong = FString(Name) == TEXT("WBP_WrongAnswerPopup");
        if (bInstruction || bWrong)
        {
            UCanvasPanel* Canvas = Cast<UCanvasPanel>(BP->WidgetTree->RootWidget);
            Require(Canvas != nullptr, TEXT("Expected popup root canvas"));
            for (UWidget* Child : Canvas->GetAllChildren())
                if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Child->Slot))
                {
                    Slot->SetAutoSize(false); Slot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
                    Slot->SetOffsets(FMargin(0.f)); Slot->SetAlignment(FVector2D::ZeroVector);
                }
        }
        if (!bInstruction && !bWrong)
            if (UCanvasPanel* Canvas = Cast<UCanvasPanel>(BP->WidgetTree->RootWidget))
                for (UWidget* Child : Canvas->GetAllChildren())
                    if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Child->Slot))
                    {
                        const FVector2D Size = Slot->GetSize();
                        Slot->SetAutoSize(false);
                        Slot->SetSize(FVector2D(FMath::Max(400.f, Size.X), FMath::Max(360.f, Size.Y)));
                    }
        BP->WidgetTree->ForEachWidget([&](UWidget* Widget)
        {
            if (UTextBlock* Text = Cast<UTextBlock>(Widget))
            {
                Text->SetAutoWrapText(true); Text->SetJustification(ETextJustify::Center);
                if (UBorderSlot* Slot = Cast<UBorderSlot>(Text->Slot)) Slot->SetHorizontalAlignment(HAlign_Fill);
                if (UVerticalBoxSlot* Slot = Cast<UVerticalBoxSlot>(Text->Slot)) Slot->SetHorizontalAlignment(HAlign_Fill);
                if (USizeBoxSlot* Slot = Cast<USizeBoxSlot>(Text->Slot)) Slot->SetHorizontalAlignment(HAlign_Fill);
            }
            if (bInstruction)
                if (UBorder* Border = Cast<UBorder>(Widget))
                {
                    Border->SetHorizontalAlignment(HAlign_Fill); Border->SetVerticalAlignment(VAlign_Center);
                    if (Border->GetFName() == TEXT("Border_102")) Border->SetPadding(FMargin(24.f, 12.f));
                }
            if (!bInstruction && !bWrong)
                if (UBorder* Border = Cast<UBorder>(Widget)) Border->SetHorizontalAlignment(HAlign_Fill);
            if (bWrong)
                if (USizeBox* Box = Cast<USizeBox>(Widget))
                    if (Cast<UTextBlock>(Box->GetContent()))
                        Box->ClearHeightOverride();
        });
        CompileAndSave(BP);
    }
    UE_LOG(LogTemp, Display, TEXT("CNA_POPUPS: responsive instruction background and wrapped popup messages saved"));
}

}

int32 UCNAReliabilityCommandlet::Main(const FString& Params)
{
    const FString Dir = FPaths::ProjectSavedDir() / TEXT("Reliability");
    IFileManager::Get().MakeDirectory(*Dir, true);
    const bool bApply = FParse::Param(*Params, TEXT("Apply"));
    try
    {
        if (FParse::Param(*Params, TEXT("FixPopupLayouts"))) { FixPopupLayouts(); return 0; }
        if (FParse::Param(*Params, TEXT("QuestFeedback"))) { ApplyQuestFeedback(); return 0; }
        if (FParse::Param(*Params, TEXT("GuidedTraining"))) { MigrateGuidedTraining(); return 0; }
        if (FParse::Param(*Params, TEXT("EnableTrainingUI"))) { EnableTrainingUI(); return 0; }
        if (FParse::Param(*Params, TEXT("SetupGuidance")))
        {
            UBlueprint* MenuWidget = Blueprint(TEXT("/Game/VRTemplate/Blueprints/WidgetMenu"));
            const FString SourceFile = FPackageName::LongPackageNameToFilename(TEXT("/Game/VRTemplate/Blueprints/WidgetMenu"), FPackageName::GetAssetPackageExtension());
            const FString Backup = FPaths::ProjectSavedDir() / TEXT("Guidance/BeforeGuidance-WidgetMenu.uasset");
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(Backup), true);
            if (!IFileManager::Get().FileExists(*Backup)) Require(IFileManager::Get().Copy(*Backup, *SourceFile) == COPY_OK, TEXT("Menu backup failed"));
            if (MenuWidget->ParentClass != UTransferHintMenuWidget::StaticClass())
            {
                MenuWidget->ParentClass = UTransferHintMenuWidget::StaticClass();
                CompileAndSave(MenuWidget);
            }
            return 0;
        }
        if (bApply)
        {
            // Preserve exact pre-migration asset bytes; the current map is deliberately not rewritten.
            for (const TCHAR* Path : { TEXT("/Game/VRTemplate/Blueprints/VRPawn"), TEXT("/Game/VRTemplate/Blueprints/Menu"), TEXT("/Game/VRTemplate/Blueprints/WidgetMenu"), TEXT("/Game/VRTemplate/Input/IMC_Menu"), TEXT("/Game/Project/Blueprints/Actors/BP_MediaDisplay"), TEXT("/Game/Project/Blueprints/Actors/BP_MediaEventHandler"), TEXT("/Game/Project/Media/MP_Display") })
            {
                const FString SourceFile = FPackageName::LongPackageNameToFilename(Path, FPackageName::GetAssetPackageExtension());
                const FString Backup = Dir / TEXT("BeforeFix") / (FString(Path).RightChop(6) + TEXT(".uasset"));
                IFileManager::Get().MakeDirectory(*FPaths::GetPath(Backup), true);
                if (!IFileManager::Get().FileExists(*Backup)) Require(IFileManager::Get().Copy(*Backup, *SourceFile) == COPY_OK, TEXT("Asset backup failed"));
            }
            MigrateInput();
            MigrateMedia();
        }
        if (FParse::Param(*Params, TEXT("FixCursorMenu"))) MigrateCursorMenu();
        if (FParse::Param(*Params, TEXT("DisablePatientConversation"))) DisablePatientConversation();
        for (const TCHAR* Path : { TEXT("/Game/VRTemplate/Blueprints/VRPawn"), TEXT("/Game/VRTemplate/Blueprints/Menu"), TEXT("/Game/VRTemplate/Blueprints/WidgetMenu"), TEXT("/Game/Project/Blueprints/Actors/BP_MediaDisplay"), TEXT("/Game/Project/Blueprints/Actors/BP_MediaEventHandler"), TEXT("/Game/Project/Blueprints/UI/WBP_MediaText") })
        {
            UBlueprint* BP = Blueprint(Path);
            FFileHelper::SaveStringToFile(DescribeBlueprint(BP), *(Dir / (BP->GetName() + TEXT(".txt"))));
        }
        UE_LOG(LogTemp, Display, TEXT("CNA_RELIABILITY: %s complete"), bApply ? TEXT("migration") : TEXT("inspection"));
        return 0;
    }
    catch (const std::exception& Error)
    {
        UE_LOG(LogTemp, Error, TEXT("CNA_RELIABILITY: %s"), UTF8_TO_TCHAR(Error.what()));
        return 1;
    }
}
