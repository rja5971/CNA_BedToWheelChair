#include "TransferGuidanceComponent.h"
#include "../Patient/PatientActor.h"
#include "../Transfer/BeltActor.h"
#include "../Transfer/WheelchairActor.h"
#include "../StateMachine/TransferStateMachine.h"
#include "BeltComponent.h"
#include "GrabComponent.h"
#include "PatientCinematicComponent.h"
#include "SeatedTransitionComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Animation/AnimSequence.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Camera/PlayerCameraManager.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "../Reliability/CNAReliabilityLibrary.h"
#include "UObject/ConstructorHelpers.h"

UTransferGuidanceComponent::UTransferGuidanceComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Game/PatientSetup/Guidance/M_TransferGhost"));
    static ConstructorHelpers::FObjectFinder<USkeletalMesh> Left(TEXT("/Game/Characters/MannequinsXR/Meshes/SKM_MannyXR_left"));
    static ConstructorHelpers::FObjectFinder<USkeletalMesh> Right(TEXT("/Game/Characters/MannequinsXR/Meshes/SKM_MannyXR_right"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> Grasp(TEXT("/Game/Characters/MannequinsXR/Animations/A_MannequinsXR_Grasp_Right"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> Seated(TEXT("/Game/Animations/SittingIdle_1__UE"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone"));
    GhostMaterial = Material.Object; LeftHandMesh = Left.Object; RightHandMesh = Right.Object;
    HandPose = Grasp.Object; ChairPose = Seated.Object;
    SphereMesh = Sphere.Object; CylinderMesh = Cylinder.Object; ConeMesh = Cone.Object;
}

void UTransferGuidanceComponent::Initialize(UTransferStateMachine* InStateMachine)
{
    UnbindEvents();
    bTransferFailed = false;
    StateMachine = InStateMachine;
    BoundPatient = StateMachine ? StateMachine->GetPatient() : nullptr;
    BoundBelt = StateMachine && StateMachine->GetBelt() ? Cast<ABeltActor>(StateMachine->GetBelt()->GetOwner()) : nullptr;
    BindEvents();
    RefreshHints();
}

void UTransferGuidanceComponent::BindEvents()
{
    if (StateMachine) StateMachine->OnTransferStateChanged.AddDynamic(this, &UTransferGuidanceComponent::OnTransferState);
    if (StateMachine) StateMachine->OnTransferFailed.AddDynamic(this, &UTransferGuidanceComponent::OnTransferFailed);
    if (BoundPatient) BoundPatient->OnPatientStateChanged.AddDynamic(this, &UTransferGuidanceComponent::OnPatientState);
    if (BoundBelt)
    {
        UBeltComponent* Belt = BoundBelt->GetBeltComponent();
        Belt->OnBeltAttachedToPatient.AddDynamic(this, &UTransferGuidanceComponent::OnBeltAttached);
        Belt->OnBeltDetachedFromPatient.AddDynamic(this, &UTransferGuidanceComponent::OnBeltDetached);
        Belt->OnBeltHandleGrabbed.AddDynamic(this, &UTransferGuidanceComponent::OnHandleGrabbed);
        Belt->OnTwoHandGrabEnded.AddDynamic(this, &UTransferGuidanceComponent::OnBeltDetached);
    }
}
void UTransferGuidanceComponent::UnbindEvents()
{
    for (UGrabComponent* HandComponent : BoundHands)
        if (IsValid(HandComponent)) { HandComponent->OnGrabStarted.RemoveAll(this); HandComponent->OnGrabEnded.RemoveAll(this); }
    BoundHands.Empty();
    if (StateMachine) StateMachine->OnTransferStateChanged.RemoveAll(this);
    if (StateMachine) StateMachine->OnTransferFailed.RemoveAll(this);
    if (BoundPatient) BoundPatient->OnPatientStateChanged.RemoveAll(this);
    if (BoundBelt)
    {
        UBeltComponent* Belt = BoundBelt->GetBeltComponent();
        Belt->OnBeltAttachedToPatient.RemoveAll(this); Belt->OnBeltDetachedFromPatient.RemoveAll(this);
        Belt->OnBeltHandleGrabbed.RemoveAll(this); Belt->OnTwoHandGrabEnded.RemoveAll(this);
    }
}
void UTransferGuidanceComponent::OnPatientState(EPatientState State) { RefreshHints(); }
void UTransferGuidanceComponent::OnTransferState(ETransferState Old, ETransferState New) { bTransferFailed = New == ETransferState::Failed; RefreshHints(); }
void UTransferGuidanceComponent::OnTransferFailed(FText Reason) { bTransferFailed = true; RefreshHints(); }
void UTransferGuidanceComponent::OnBeltAttached(AActor* Patient) { RefreshHints(); }
void UTransferGuidanceComponent::OnBeltDetached() { RefreshHints(); }
void UTransferGuidanceComponent::OnHandleGrabbed(FName Handle, AActor* Owner) { RefreshHints(); }
void UTransferGuidanceComponent::OnHandGrab(AActor* Actor, FName Bone, FVector Location) { RefreshHints(); }
void UTransferGuidanceComponent::OnHandRelease(AActor* Actor) { RefreshHints(); }
void UTransferGuidanceComponent::BindHandEvents()
{
    for (TActorIterator<APawn> It(GetWorld()); It; ++It)
    {
        TArray<UGrabComponent*> Hands; It->GetComponents(Hands);
        for (UGrabComponent* Grabber : Hands)
            if (!BoundHands.Contains(Grabber))
            {
                BoundHands.Add(Grabber);
                Grabber->OnGrabStarted.AddDynamic(this, &UTransferGuidanceComponent::OnHandGrab);
                Grabber->OnGrabEnded.AddDynamic(this, &UTransferGuidanceComponent::OnHandRelease);
            }
    }
}

bool UTransferGuidanceComponent::AreHintsEnabled() const
{
    UGameInstance* Instance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
    const UTransferHintsSubsystem* Settings = Instance ? Instance->GetSubsystem<UTransferHintsSubsystem>() : nullptr;
    return Settings ? Settings->AreHintsEnabled() : bLocalEnabled;
}
void UTransferGuidanceComponent::SetHintsEnabled(bool bEnabled)
{
    bLocalEnabled = bEnabled;
    UGameInstance* Instance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
    if (Instance) Instance->GetSubsystem<UTransferHintsSubsystem>()->SetHintsEnabled(bEnabled);
    RefreshHints();
}
bool UTransferGuidanceComponent::IsMenuOpen() const
{
    APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
    return IsValid(UCNAReliabilityLibrary::ObjectProperty(Pawn, TEXT("MenuReference")));
}
bool UTransferGuidanceComponent::IsActorHeld(AActor* Actor) const
{
    if (!Actor) return false;
    for (TActorIterator<APawn> It(GetWorld()); It; ++It)
    {
        TArray<UGrabComponent*> Hands; It->GetComponents(Hands);
        for (const UGrabComponent* Grabber : Hands) if (Grabber->GetGrabbedActor() == Actor) return true;
    }
    return false;
}

void UTransferGuidanceComponent::RefreshHints()
{
    BindHandEvents();
    const ETransferHintStep Previous = Step;
    Step = ETransferHintStep::None;
    SelectedChair.Reset();
    if (!StateMachine || !BoundPatient || !BoundBelt || bTransferFailed || !AreHintsEnabled() || IsMenuOpen()) { HideVisuals(); return; }
    const EPatientInteractionPhase Phase = BoundPatient->GetInteractionPhase();
    if (Phase == EPatientInteractionPhase::BedSeating || Phase == EPatientInteractionPhase::WheelchairSeating
        || Phase == EPatientInteractionPhase::Complete || StateMachine->GetCurrentState() == ETransferState::Failed
        || StateMachine->GetCurrentState() == ETransferState::Complete || BoundPatient->GetPatientState() == EPatientState::Injured
        || BoundPatient->GetPatientCinematicComponent()->IsCinematicActive()) { HideVisuals(); return; }
    if (Phase == EPatientInteractionPhase::BedPreparation)
    {
        if (!BoundPatient->GetGrabbableBoneNames().IsEmpty())
            Step = BoundPatient->IsNeckSupported() ? ETransferHintStep::SitUp : ETransferHintStep::GrabPatient;
    }
    else if (!BoundBelt->GetBeltComponent()->IsAttached())
    {
        Step = IsActorHeld(BoundBelt) ? ETransferHintStep::AttachBelt : ETransferHintStep::PickUpBelt;
    }
    else if (!BoundBelt->GetBeltComponent()->IsBeingLifted())
    {
        Step = ETransferHintStep::GrabBeltHandle;
    }
    else
    {
        AWheelchairActor* Chair = StateMachine->GetGuidanceWheelchair();
        if (!IsValid(Chair) || !Chair->IsReadyToReceive()) Chair = StateMachine->GetWheelchair();
        if (!IsValid(Chair) || !Chair->IsReadyToReceive())
        {
            Chair = nullptr;
            float Best = TNumericLimits<float>::Max();
            for (TActorIterator<AWheelchairActor> It(GetWorld()); It; ++It)
                if (It->IsReadyToReceive())
                {
                    const float Score = It->GetSeatDistanceSquared(BoundPatient->GetPelvisLocation());
                    if (Score < Best) { Best = Score; Chair = *It; }
                }
        }
        SelectedChair = Chair;
        if (Chair && StateMachine->GetGuidanceWheelchair() == Chair && StateMachine->GetCurrentState() == ETransferState::WheelchairTransfer && Chair->IsLocationInSeatArea(BoundPatient->GetPelvisLocation())) Step = ETransferHintStep::ReleaseToSeat;
        else if (Chair && StateMachine->GetCurrentState() == ETransferState::WheelchairTransfer) Step = ETransferHintStep::MoveToSeat;
        else Step = ETransferHintStep::LiftAndCarry;
    }
    if (Previous != Step) LoopElapsed = 0.f;
    if (Step != ETransferHintStep::None && !Hand) CreateVisuals();
    UpdateVisuals();
}

FText UTransferGuidanceComponent::GetHintText() const
{
    switch (Step)
    {
    case ETransferHintStep::GrabPatient: return FText::FromString(TEXT("Grab here"));
    case ETransferHintStep::SitUp: return FText::FromString(TEXT("Hold grip and lift along the arrow"));
    case ETransferHintStep::PickUpBelt: return FText::FromString(TEXT("Pick up the belt"));
    case ETransferHintStep::AttachBelt: return FText::FromString(TEXT("Bring the belt here"));
    case ETransferHintStep::GrabBeltHandle: return FText::FromString(TEXT("Grab the front belt handle"));
    case ETransferHintStep::LiftAndCarry: return FText::FromString(SelectedChair.IsValid() ? TEXT("Lift and guide toward the chair") : TEXT("Hold the belt - no ready chair"));
    case ETransferHintStep::MoveToSeat: return FText::FromString(TEXT("Move toward the seat"));
    case ETransferHintStep::ReleaseToSeat: return FText::FromString(TEXT("Release to seat"));
    default: return FText::GetEmpty();
    }
}

FTransform UTransferGuidanceComponent::GrabTarget() const
{
    if (Step == ETransferHintStep::GrabPatient || Step == ETransferHintStep::SitUp)
    {
        const TArray<FName> Bones = BoundPatient->GetGrabbableBoneNames();
        FName TargetBone = Bones.IsEmpty() ? NAME_None : Bones[0];
        for (FName Bone : Bones) if (Bone.ToString().Contains(TEXT("neck"))) { TargetBone = Bone; break; }
        return BoundPatient->GetPatientMesh()->GetSocketTransform(TargetBone);
    }
    if (Step == ETransferHintStep::AttachBelt) return BoundPatient->GetBeltAttachTransform();
    if (Step == ETransferHintStep::PickUpBelt) return BoundBelt->GetActorTransform();
    return FTransform(BoundBelt->GetActorQuat(), BoundBelt->GetHandleWorldLocation());
}

void UTransferGuidanceComponent::CreateVisuals()
{
    if (!GhostMaterial) { UE_LOG(LogTemp, Error, TEXT("TransferGuidance: Ghost material missing from package.")); return; }
    GhostMID = UMaterialInstanceDynamic::Create(GhostMaterial, this);
    GhostMID->SetScalarParameterValue(TEXT("Opacity"), VisualSettings.GhostOpacity);
    auto Setup = [&](UPrimitiveComponent* Component)
    {
        GetOwner()->AddInstanceComponent(Component);
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Component->SetGenerateOverlapEvents(false); Component->SetCanEverAffectNavigation(false);
        Component->SetCastShadow(false); Component->SetVisibility(false);
        Component->RegisterComponent();
    };
    Hand = NewObject<USkeletalMeshComponent>(GetOwner());
    Hand->SetSkeletalMesh(RightHandMesh); Hand->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    Hand->SetAnimation(HandPose); Setup(Hand);
    Hand->SetPosition(HandPose ? HandPose->GetPlayLength() * .65f : 0.f);
    Hand->TickAnimation(0.f, false); Hand->RefreshBoneTransforms(); Hand->SetComponentTickEnabled(false);
    for (int32 Index = 0; Index < Hand->GetNumMaterials(); ++Index) Hand->SetMaterial(Index, GhostMID);
    SeatedGhost = NewObject<USkeletalMeshComponent>(GetOwner());
    SeatedGhost->SetSkeletalMesh(BoundPatient->GetPatientMesh()->GetSkeletalMeshAsset());
    SeatedGhost->SetAnimationMode(EAnimationMode::AnimationSingleNode); SeatedGhost->SetAnimation(ChairPose);
    Setup(SeatedGhost); SeatedGhost->SetPosition(0.f); SeatedGhost->TickAnimation(0.f, false);
    SeatedGhost->RefreshBoneTransforms(); SeatedGhost->SetComponentTickEnabled(false);
    for (int32 Index = 0; Index < SeatedGhost->GetNumMaterials(); ++Index) SeatedGhost->SetMaterial(Index, GhostMID);
    auto MakeStatic = [&](UStaticMesh* Mesh)
    {
        UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(GetOwner());
        Component->SetStaticMesh(Mesh); Component->SetMaterial(0, GhostMID); Setup(Component); return Component;
    };
    Target = MakeStatic(SphereMesh); ArrowShaft = MakeStatic(CylinderMesh); ArrowHead = MakeStatic(ConeMesh);
    UStaticMeshComponent* BeltMesh = Cast<UStaticMeshComponent>(BoundBelt->GetGrabbableComponent());
    BeltGhost = MakeStatic(BeltMesh ? BeltMesh->GetStaticMesh() : nullptr);
    for (int32 Index = 0; Index < BeltGhost->GetNumMaterials(); ++Index) BeltGhost->SetMaterial(Index, GhostMID);
    Label = NewObject<UTextRenderComponent>(GetOwner()); Setup(Label);
    Label->SetHorizontalAlignment(EHTA_Center); Label->SetVerticalAlignment(EVRTA_TextCenter);
    Label->SetWorldSize(VisualSettings.TextSize);
}

void UTransferGuidanceComponent::HideVisuals()
{
    for (USceneComponent* Component : TArray<USceneComponent*>{ Hand.Get(), SeatedGhost.Get(), Target.Get(), BeltGhost.Get(), ArrowShaft.Get(), ArrowHead.Get(), Label.Get() })
        if (Component) Component->SetVisibility(false);
}

void UTransferGuidanceComponent::UpdateVisuals()
{
    if (!Hand || Step == ETransferHintStep::None || bTransferFailed || !AreHintsEnabled() || IsMenuOpen()) { HideVisuals(); return; }
    // Enforce locks every frame, including the interval between 10 Hz decisions.
    const auto Phase = BoundPatient->GetInteractionPhase();
    if (Phase == EPatientInteractionPhase::BedSeating || Phase == EPatientInteractionPhase::WheelchairSeating || Phase == EPatientInteractionPhase::Complete
        || BoundPatient->GetPatientCinematicComponent()->IsCinematicActive()
        || BoundPatient->GetPatientState() == EPatientState::Injured
        || StateMachine->GetCurrentState() == ETransferState::Failed || StateMachine->GetCurrentState() == ETransferState::Complete) { HideVisuals(); return; }
    if (!SelectedChair.IsValid() && (Step == ETransferHintStep::MoveToSeat || Step == ETransferHintStep::ReleaseToSeat)) { RefreshHints(); return; }
    if (SelectedChair.IsValid())
    {
        const bool bReleaseNow = SelectedChair->IsReadyToReceive() && StateMachine->GetGuidanceWheelchair() == SelectedChair.Get()
            && StateMachine->GetCurrentState() == ETransferState::WheelchairTransfer
            && SelectedChair->IsLocationInSeatArea(BoundPatient->GetPelvisLocation());
        // A release cue must never linger outside the real commit zone between polls.
        if (!SelectedChair->IsReadyToReceive() || bReleaseNow != (Step == ETransferHintStep::ReleaseToSeat)) { RefreshHints(); return; }
    }
    const bool bChair = SelectedChair.IsValid();
    const FTransform Anchor = GrabTarget();
    FVector Position = Anchor.GetLocation();
    if (bChair) Position = SelectedChair->GetTargetSeatTransform().GetLocation();
    const bool bReady = Step == ETransferHintStep::ReleaseToSeat;
    const FLinearColor Color = bReady ? VisualSettings.ReadyColor : VisualSettings.HintColor;
    GhostMID->SetVectorParameterValue(TEXT("Tint"), Color);
    const float Cycle = FMath::Fmod(LoopElapsed / FMath::Max(.5f, VisualSettings.LoopDuration), 1.f);
    const float Pulse = .85f + .15f * FMath::Sin(Cycle * 2.f * PI);
    Target->SetWorldLocation(Position); Target->SetWorldScale3D(FVector(bChair ? .75f : .16f) * Pulse);
    if (bChair) Target->SetWorldScale3D(FVector(.65f, .65f, .025f) * Pulse);
    Target->SetVisibility(true);
    BeltGhost->SetVisibility(Step == ETransferHintStep::AttachBelt || Step == ETransferHintStep::PickUpBelt);
    if (Step == ETransferHintStep::AttachBelt)
    {
        // Attachment preserves belt scale and starts at world-zero rotation.
        BeltGhost->SetWorldTransform(FTransform(FQuat::Identity, Anchor.GetLocation(), BoundBelt->GetActorScale3D()));
        BeltGhost->SetVisibility(true);
    }
    else if (Step == ETransferHintStep::PickUpBelt)
    {
        BeltGhost->SetWorldTransform(BoundBelt->GetActorTransform());
        BeltGhost->SetWorldScale3D(BoundBelt->GetActorScale3D() * 1.025f);
        BeltGhost->SetVisibility(true);
    }
    const bool bGrab = Step == ETransferHintStep::GrabPatient || Step == ETransferHintStep::PickUpBelt || Step == ETransferHintStep::GrabBeltHandle;
    Hand->SetVisibility(bGrab);
    if (bGrab)
    {
        USkeletalMesh* HandMesh = RightHandMesh;
        float Closest = TNumericLimits<float>::Max();
        if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0))
        {
            TArray<UGrabComponent*> Hands; Pawn->GetComponents(Hands);
            for (UGrabComponent* Grabber : Hands)
                if (!Grabber->IsGrabbing() && Grabber->GetTraceOrigin())
                {
                    const float Distance = FVector::DistSquared(Grabber->GetTraceOrigin()->GetComponentLocation(), Anchor.GetLocation());
                    if (Distance < Closest)
                    {
                        Closest = Distance;
                        HandMesh = Grabber->GetName().Contains(TEXT("Left")) ? LeftHandMesh.Get() : RightHandMesh.Get();
                    }
                }
        }
        if (Hand->GetSkeletalMeshAsset() != HandMesh)
        {
            Hand->SetSkeletalMesh(HandMesh); Hand->SetAnimation(HandPose);
            Hand->SetPosition(HandPose ? HandPose->GetPlayLength() * .65f : 0.f);
            Hand->TickAnimation(0.f, false); Hand->RefreshBoneTransforms();
            for (int32 Index = 0; Index < Hand->GetNumMaterials(); ++Index) Hand->SetMaterial(Index, GhostMID);
        }
        const FTransform Pose = Step == ETransferHintStep::GrabPatient ? VisualSettings.HeadHandPose : VisualSettings.BeltHandPose;
        const FTransform Contact = Pose * FTransform(Anchor.GetRotation(), Anchor.GetLocation());
        Hand->SetWorldTransform(Contact);
        const FName Wrist = HandMesh == LeftHandMesh ? FName(TEXT("hand_l")) : FName(TEXT("hand_r"));
        if (Hand->DoesSocketExist(Wrist)) Hand->AddWorldOffset(Contact.GetLocation() - Hand->GetSocketLocation(Wrist));
        Hand->SetVisibility(true);
    }
    FVector From = Anchor.GetLocation(), To = From;
    if (Step == ETransferHintStep::SitUp) To += VisualSettings.SitUpDirection;
    else if (Step == ETransferHintStep::AttachBelt) { From = BoundBelt->GetActorLocation(); To = Position; }
    else if (Step == ETransferHintStep::LiftAndCarry || Step == ETransferHintStep::MoveToSeat)
    {
        From = BoundPatient->GetPelvisLocation();
        To = bChair ? Position : From + VisualSettings.LiftDirection;
        if (Step == ETransferHintStep::LiftAndCarry && Cycle < .45f) To = From + VisualSettings.LiftDirection;
    }
    const FVector Delta = To - From;
    const bool bDrawArrow = Delta.SizeSquared() > 4.f && !bReady;
    ArrowShaft->SetVisibility(bDrawArrow); ArrowHead->SetVisibility(bDrawArrow);
    if (bDrawArrow)
    {
        const FVector Direction = Delta.GetSafeNormal();
        const FVector Tip = FMath::Lerp(From, To, .35f + .65f * FMath::Min(Cycle / .75f, 1.f));
        const float Length = FMath::Min(Delta.Size(), 28.f);
        const FQuat Rotation = FQuat::FindBetweenNormals(FVector::UpVector, Direction);
        ArrowShaft->SetWorldLocationAndRotation(Tip - Direction * Length * .5f, Rotation);
        ArrowShaft->SetWorldScale3D(FVector(.018f, .018f, Length / 100.f));
        ArrowHead->SetWorldLocationAndRotation(Tip, Rotation); ArrowHead->SetWorldScale3D(FVector(.07f, .07f, .1f));
        ArrowShaft->SetVisibility(true); ArrowHead->SetVisibility(true);
    }
    SeatedGhost->SetVisibility(bChair && ChairPose);
    if (bChair && ChairPose)
    {
        const FTransform Seat = SelectedChair->GetTargetSeatTransform();
        const FName Pelvis = BoundPatient->GetPelvisBoneName();
        SeatedGhost->SetWorldTransform(FTransform(Seat.GetRotation(), Seat.GetLocation(), BoundPatient->GetPatientMesh()->GetComponentScale()));
        if (SeatedGhost->DoesSocketExist(Pelvis))
        {
            const float Yaw = Seat.Rotator().Yaw - SeatedGhost->GetSocketRotation(Pelvis).Yaw;
            SeatedGhost->AddWorldRotation(FRotator(0, Yaw, 0));
            SeatedGhost->AddWorldOffset(Seat.GetLocation() - SeatedGhost->GetSocketLocation(Pelvis));
        }
        SeatedGhost->SetVisibility(true);
    }
    Label->SetText(GetHintText()); Label->SetTextRenderColor(Color.ToFColor(true));
    Label->SetWorldLocation(Position + (bChair ? VisualSettings.ChairLabelOffset : VisualSettings.LabelOffset));
    APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0);
    if (Camera) Label->SetWorldRotation((Camera->GetCameraLocation() - Label->GetComponentLocation()).Rotation());
    Label->SetVisibility(true);
}

void UTransferGuidanceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(DeltaTime, TickType, TickFunction);
    LoopElapsed += DeltaTime; RefreshElapsed += DeltaTime;
    if (RefreshElapsed >= .1f) { RefreshElapsed = 0.f; RefreshHints(); }
    else UpdateVisuals();
}
void UTransferGuidanceComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    UnbindEvents(); HideVisuals();
    for (USceneComponent* Component : TArray<USceneComponent*>{ Hand.Get(), SeatedGhost.Get(), Target.Get(), BeltGhost.Get(), ArrowShaft.Get(), ArrowHead.Get(), Label.Get() })
        if (Component) Component->DestroyComponent();
    Super::EndPlay(Reason);
}
