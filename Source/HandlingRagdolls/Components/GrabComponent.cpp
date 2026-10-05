#include "GrabComponent.h"
#include "PatientPhysicsHandleComponent.h"
#include "PatientBedSupportComponent.h"
#include "../Patient/PatientActor.h"
#include "../Transfer/BeltActor.h"
#include "../Interfaces/IGrabbable.h"
#include "Components/SkeletalMeshComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CoreDelegates.h"
#include "MotionControllerComponent.h"
#include "HeadMountedDisplayFunctionLibrary.h"

UGrabComponent::UGrabComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UGrabComponent::BeginPlay()
{
    Super::BeginPlay();
    PhysicsHandle = NewObject<UPatientPhysicsHandleComponent>(GetOwner());
    GetOwner()->AddInstanceComponent(PhysicsHandle);
    PhysicsHandle->RegisterComponent();
    PhysicsHandle->AddTickPrerequisiteComponent(this);
    BackgroundHandle = FCoreDelegates::ApplicationWillEnterBackgroundDelegate.AddUObject(this, &UGrabComponent::SuspendInteraction);
    DeactivateHandle = FCoreDelegates::ApplicationWillDeactivateDelegate.AddUObject(this, &UGrabComponent::SuspendInteraction);
    ForegroundHandle = FCoreDelegates::ApplicationHasEnteredForegroundDelegate.AddUObject(this, &UGrabComponent::ResumeInteraction);
    ReactivateHandle = FCoreDelegates::ApplicationHasReactivatedDelegate.AddUObject(this, &UGrabComponent::ResumeInteraction);
}

void UGrabComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    ReleaseRagdoll();
    FCoreDelegates::ApplicationWillEnterBackgroundDelegate.Remove(BackgroundHandle);
    FCoreDelegates::ApplicationWillDeactivateDelegate.Remove(DeactivateHandle);
    FCoreDelegates::ApplicationHasEnteredForegroundDelegate.Remove(ForegroundHandle);
    FCoreDelegates::ApplicationHasReactivatedDelegate.Remove(ReactivateHandle);
    if (PhysicsHandle) PhysicsHandle->DestroyComponent();
    Super::EndPlay(Reason);
}

void UGrabComponent::TickComponent(float DeltaTime, ELevelTick Type, FActorComponentTickFunction* Function)
{
    Super::TickComponent(DeltaTime, Type, Function);
    if (bInteractionSuspended) return;
    if (const UMotionControllerComponent* Controller = Cast<UMotionControllerComponent>(TraceOrigin))
        if (UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled() && !Controller->IsTracked())
        { CancelGrab(); return; }
    if (IsGrabbing()) UpdateGrabTarget(DeltaTime);
    else if (bGripHeld)
    {
        RetryElapsed += DeltaTime;
        if (RetryElapsed >= 0.08f) { RetryElapsed = 0.0f; TryGrabRagdoll(); }
    }
}

float UGrabComponent::GetActiveForceLimit() const
{
    const UPatientPhysicsHandleComponent* Handle = Cast<UPatientPhysicsHandleComponent>(PhysicsHandle);
    return Handle ? Handle->GetPatientForceLimit() : 0.0f;
}

bool UGrabComponent::TryGrabRagdoll()
{
    if (bInteractionSuspended) return false;
    bGripHeld = true;
    if (IsGrabbing() || !PhysicsHandle) return false;
    AActor* Actor = nullptr;
    FName Bone;
    FVector Location;
    if (!FindGrabTarget(Actor, Bone, Location)) return false;
    IIGrabbable* Target = Cast<IIGrabbable>(Actor);
    if (!Target) return false;
    FName PhysicsBone = Target->GetGrabBoneOverride();
    if (PhysicsBone.IsNone()) PhysicsBone = Bone;
    UPrimitiveComponent* Component = Target->GetGrabbableComponent();
    if (!Component) return false;
    APatientActor* Patient = Cast<APatientActor>(Actor);
    bPatientBodyGrab = Patient != nullptr;
    GrabbedActor = Actor;
    GrabbedBoneName = PhysicsBone;
    GrabLocation = Location;
    PreviousTarget = Location;
    const FVector Hand = TraceOrigin ? TraceOrigin->GetComponentLocation() : GetOwner()->GetActorLocation();
    PreviousHandPosition = Hand;
    HandContactOffset = Location - Hand;
    if (USkeletalMeshComponent* Mesh = Cast<USkeletalMeshComponent>(Component))
        if (FBodyInstance* Body = Mesh->GetBodyInstance(PhysicsBone))
            BodyLocalContact = Body->GetUnrealWorldTransform().InverseTransformPosition(Location);
    // The callback makes a loose belt simulated, or starts animation-owned carry.
    // Do not constrain a kinematic mesh before that ownership decision is made.
    Target->OnGrabbed(this, Bone, Location);
    if (GrabbedActor != Actor) return false;
    Component = Target->GetGrabbableComponent();
    if (Target->ShouldUsePhysicsHandle())
    {
        if (!Component || !Component->IsSimulatingPhysics(PhysicsBone))
        {
            ReleaseRagdoll();
            bGripHeld = true;
            return false;
        }
        UPatientPhysicsHandleComponent* Handle = CastChecked<UPatientPhysicsHandleComponent>(PhysicsHandle);
        float ForceLimit = 0.0f;
        if (Patient)
        {
            UPatientBedSupportComponent* Bed = Patient->GetBedSupportComponent();
            FBodyInstance* Body = Patient->GetPatientMesh()->GetBodyInstance(PhysicsBone);
            const float Mass = Body ? Body->GetBodyMass() : 5.0f;
            const bool bTorso = Bed && Bed->IsTorsoGrab(PhysicsBone);
            const bool bHead = Bone.ToString().Contains(TEXT("head")) || Bone.ToString().Contains(TEXT("neck"));
            ForceLimit = bTorso ? 100000.0f : bHead ? 5000.0f : FMath::Clamp(Mass * 2200.0f, 4000.0f, 22000.0f);
            if (Bed && !bTorso && !bHead)
            {
                // A distal grip lifts a connected limb, not just the small hand
                // or foot body. Size the finite force for that regional load.
                ForceLimit = FMath::Clamp(Bed->GetRegionMass(PhysicsBone) * 1200.0f, 6000.0f, 22000.0f);
            }
        }
        Handle->SetPatientForceLimit(ForceLimit);
        Handle->SetLinearStiffness(Patient ? 5000.0f : GrabLinearStiffness);
        Handle->SetLinearDamping(Patient ? 350.0f : GrabLinearDamping);
        Handle->SetAngularStiffness(GrabAngularStiffness);
        Handle->SetAngularDamping(GrabAngularDamping);
        Handle->SetInterpolationSpeed(Patient ? 15.0f : GrabInterpolationSpeed);
        if (Target->RequiresRotationConstraint())
            Handle->GrabComponentAtLocationWithRotation(Component, PhysicsBone, Location, Component->GetComponentRotation());
        else Handle->GrabComponentAtLocation(Component, PhysicsBone, Location);
    }
    OnGrabStarted.Broadcast(Actor, PhysicsBone, Location);
    UE_LOG(LogTemp, Log, TEXT("PatientGrab: Acquired %s / %s; force cap %.0f."), *GetNameSafe(Actor), *PhysicsBone.ToString(), GetActiveForceLimit());
    return true;
}

void UGrabComponent::SuspendInteraction()
{
    bInteractionSuspended = true;
    CancelGrab();
}

void UGrabComponent::ResumeInteraction()
{
    bInteractionSuspended = false;
}

void UGrabComponent::ReleaseRagdoll()
{
    bGripHeld = false;
    RetryElapsed = 0.0f;
    if (!IsGrabbing()) return;
    AActor* Actor = GrabbedActor;
    // Clear before callbacks to make release reentrant-safe during ownership transitions.
    GrabbedActor = nullptr;
    GrabbedBoneName = NAME_None;
    bPatientBodyGrab = false;
    if (PhysicsHandle) PhysicsHandle->ReleaseComponent();
    if (IIGrabbable* Target = Cast<IIGrabbable>(Actor)) Target->OnReleased(this);
    OnGrabEnded.Broadcast(Actor);
}

bool UGrabComponent::FindGrabTarget(AActor*& OutActor, FName& OutBone, FVector& OutLocation) const
{
    if (!GetOwner()) return false;
    const FVector Start = TraceOrigin ? TraceOrigin->GetComponentLocation() : GetOwner()->GetActorLocation();
    const FVector Forward = TraceOrigin ? TraceOrigin->GetForwardVector() : FVector::ForwardVector;
    TArray<FHitResult> Hits;
    TArray<AActor*> Ignore { GetOwner() };
    TArray<TEnumAsByte<EObjectTypeQuery>> Types;
    Types.Add(UEngineTypes::ConvertToObjectType(ECC_PhysicsBody));
    Types.Add(UEngineTypes::ConvertToObjectType(ECC_WorldDynamic));
    UKismetSystemLibrary::SphereTraceMultiForObjects(GetWorld(), Start, Start + Forward * GrabRadius,
        GrabRadius, Types, false, Ignore, EDrawDebugTrace::None, Hits, true);
    float Closest = FLT_MAX;
    int32 BestPriority = -1;
    TSet<APatientActor*> Patients;
    for (const FHitResult& Hit : Hits)
    {
        AActor* Actor = Hit.GetActor();
        IIGrabbable* Target = Cast<IIGrabbable>(Actor);
        if (!Target) continue;
        if (APatientActor* Patient = Cast<APatientActor>(Actor))
        {
            Patients.Add(Patient);
            continue;
        }
        // Initial overlaps often report the sphere center rather than a surface contact.
        FVector Contact = Hit.ImpactPoint;
        if (Hit.bStartPenetrating && Hit.GetComponent())
            Hit.GetComponent()->GetClosestPointOnCollision(Start, Contact, Hit.BoneName);
        if (!Target->CanBeGrabbed(Hit.BoneName, Contact)) continue;
        const float Distance = FVector::Dist(Start, Contact);
        if (Distance > GrabRadius * 2.0f) continue;
        const ABeltActor* Belt = Cast<ABeltActor>(Actor);
        const int32 Priority = Belt && FVector::Dist(Start, Belt->GetHandleWorldLocation()) <= GrabRadius ? 1 : 0;
        if (Priority > BestPriority || (Priority == BestPriority && Distance < Closest))
        {
            BestPriority = Priority;
            Closest = Distance;
            OutActor = Actor;
            OutBone = Hit.BoneName;
            OutLocation = Contact;
        }
    }
    // A skeletal sweep can report only the first overlapping body. Resolve all
    // eligible bodies on each contacted patient, especially at feet and wrists
    // where adjacent capsules overlap. COM distance breaks inside-body ties.
    if (BestPriority < 1)
        for (APatientActor* Patient : Patients)
        {
            USkeletalMeshComponent* Mesh = Patient->GetPatientMesh();
            if (!Mesh || !Mesh->GetPhysicsAsset()) continue;
            for (USkeletalBodySetup* Setup : Mesh->GetPhysicsAsset()->SkeletalBodySetups)
            {
                const FName Bone = Setup->BoneName;
                if (!Patient->CanBeGrabbed(Bone, Start)) continue;
                FVector Contact;
                const float Distance = Mesh->GetClosestPointOnCollision(Start, Contact, Bone);
                if (Distance < 0.0f || Distance > GrabRadius) continue;
                FBodyInstance* Body = Mesh->GetBodyInstance(Bone);
                const float Score = Distance + (Body ? FVector::Dist(Start, Body->GetCOMPosition()) * 0.001f : 0.0f);
                if (Score < Closest)
                {
                    Closest = Score;
                    OutActor = Patient;
                    OutBone = Bone;
                    OutLocation = Contact;
                }
            }
        }
    return OutActor != nullptr;
}

void UGrabComponent::UpdateGrabTarget(float DeltaTime)
{
    if (!IsValid(GrabbedActor)) { ReleaseRagdoll(); return; }
    // Attachment transfers the loose belt from its rigid body to the patient's
    // handle. Retire the obsolete constraint while preserving the physical grip
    // latch, so acquisition can retry against the newly available handle.
    if (ABeltActor* Belt = Cast<ABeltActor>(GrabbedActor))
        if (PhysicsHandle && PhysicsHandle->GetGrabbedComponent()
            && PhysicsHandle->GetGrabbedComponent() != Belt->GetGrabbableComponent())
        {
            const bool bWasHeld = bGripHeld;
            ReleaseRagdoll();
            bGripHeld = bWasHeld;
            return;
        }
    if (!PhysicsHandle || !PhysicsHandle->GetGrabbedComponent()) return; // animated belt carry
    const FVector Hand = TraceOrigin ? TraceOrigin->GetComponentLocation() : GetOwner()->GetActorLocation();
    FVector Desired = Hand + HandContactOffset;
    if (bPatientBodyGrab)
    {
        APatientActor* Patient = Cast<APatientActor>(GrabbedActor);
        USkeletalMeshComponent* Mesh = Patient ? Patient->GetPatientMesh() : nullptr;
        FBodyInstance* Body = Mesh ? Mesh->GetBodyInstance(GrabbedBoneName) : nullptr;
        if (!Body || !Body->IsInstanceSimulatingPhysics()) { ReleaseRagdoll(); return; }
        const FVector Contact = Body->GetUnrealWorldTransform().TransformPosition(BodyLocalContact);
        Desired = Contact + (Desired - Contact).GetClampedToMaxSize(12.0f);
        if (UPatientBedSupportComponent* Bed = Patient->GetBedSupportComponent())
            if (Bed->IsBedControlActive())
            {
                Bed->MoveSupportWithHand(GrabbedBoneName, Hand - PreviousHandPosition);
                Desired = Bed->ConstrainHandTarget(GrabbedBoneName, Desired, Contact);
            }
        Desired = Contact + (Desired - Contact).GetClampedToMaxSize(12.0f);
        Desired = PreviousTarget + (Desired - PreviousTarget).GetClampedToMaxSize(150.0f * DeltaTime);
        PhysicsHandle->SetTargetLocation(Desired);
    }
    else
    {
        IIGrabbable* Target = Cast<IIGrabbable>(GrabbedActor);
        if (Target && Target->RequiresRotationConstraint())
            PhysicsHandle->SetTargetLocationAndRotation(Desired, TraceOrigin ? TraceOrigin->GetComponentRotation() : FRotator::ZeroRotator);
        else PhysicsHandle->SetTargetLocation(Desired);
    }
    PreviousTarget = Desired;
    PreviousHandPosition = Hand;
}
