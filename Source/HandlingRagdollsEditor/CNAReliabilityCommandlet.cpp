#include "CNAReliabilityCommandlet.h"
#include <stdexcept>
#include "Engine/Blueprint.h"
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
#include "Reliability/VRWidgetInputComponent.h"
#include "Reliability/TrainingMediaControllerComponent.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_CallDelegate.h"
#include "K2Node_Self.h"
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
}

int32 UCNAReliabilityCommandlet::Main(const FString& Params)
{
    const FString Dir = FPaths::ProjectSavedDir() / TEXT("Reliability");
    IFileManager::Get().MakeDirectory(*Dir, true);
    const bool bApply = FParse::Param(*Params, TEXT("Apply"));
    try
    {
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
