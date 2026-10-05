#include "PatientBedSupportComponent.h"
#include "PatientPhysicsComponent.h"
#include "SeatedTransitionComponent.h"
#include "../Patient/PatientActor.h"
#include "../Patient/PatientBedAnimInstance.h"
#include "Animation/PoseSnapshot.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"

UPatientBedSupportComponent::UPatientBedSupportComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UPatientBedSupportComponent::Initialize(APatientActor* InPatient)
{
	Patient = InPatient;
	if (!bEnabled || !Patient || !BedActor) return;
	USkeletalMeshComponent* Mesh = Patient->GetPatientMesh();
	if (!Mesh || !Mesh->GetSkeletalMeshAsset() || !Mesh->GetPhysicsAsset()) return;
	Patient->GetPatientPhysicsComponent()->ClearHeldPose();
	Mesh->SetAllBodiesSimulatePhysics(false);
	Mesh->SetAllBodiesPhysicsBlendWeight(0.0f, false);
	Mesh->bEnableUpdateRateOptimizations = false;
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	if (!MattressCollision)
	{
		// A simple authored mattress avoids internal furniture triangles and frame
		// cavities catching limbs. Keep the original furniture collision for tools.
		TArray<UPrimitiveComponent*> Furniture;
		BedActor->GetComponents(Furniture);
		for (UPrimitiveComponent* Part : Furniture)
			Part->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Ignore);
		MattressCollision = NewObject<UBoxComponent>(Patient, TEXT("PatientMattressCollision"));
		Patient->AddInstanceComponent(MattressCollision);
		MattressCollision->SetBoxExtent((MattressMax - MattressMin) * 0.5f);
		MattressCollision->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
		MattressCollision->SetCollisionObjectType(ECC_WorldStatic);
		MattressCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
		MattressCollision->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
		MattressCollision->SetGenerateOverlapEvents(false);
		MattressCollision->RegisterComponent();
		MattressCollision->AttachToComponent(BedActor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
		MattressCollision->SetRelativeLocation((MattressMax + MattressMin) * 0.5f);
	}
	Mesh->TickAnimation(0.0f, false);
	Mesh->RefreshBoneTransforms();
	FPoseSnapshot Snapshot;
	Mesh->SnapshotPose(Snapshot);
	TargetPose = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton().GetRefBonePose();
	const FReferenceSkeleton& Ref = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
	for (int32 I = 0; I < Snapshot.BoneNames.Num(); ++I)
	{
		int32 Index = Ref.FindBoneIndex(Snapshot.BoneNames[I]);
		if (TargetPose.IsValidIndex(Index)) TargetPose[Index] = Snapshot.LocalTransforms[I];
	}
	if (!Patient->GetPatientPhysicsComponent()->RestPoseAnimation)
	{
		// The neutral mannequin spreads its arms. Start with relaxed arms beside
		// the torso, rather than lifting the entire patient to clear those hands.
		TArray<FTransform> ComponentPose;
		ComponentPose.SetNum(TargetPose.Num());
		for (int32 Index = 0; Index < TargetPose.Num(); ++Index)
		{
			const int32 Parent = Ref.GetParentIndex(Index);
			FTransform Component = Parent == INDEX_NONE ? TargetPose[Index] : TargetPose[Index] * ComponentPose[Parent];
			const FName Bone = Ref.GetBoneName(Index);
			if (Bone == TEXT("upperarm_l") || Bone == TEXT("upperarm_r"))
			{
				const int32 Child = Ref.FindBoneIndex(Bone == TEXT("upperarm_l") ? TEXT("lowerarm_l") : TEXT("lowerarm_r"));
				if (Child != INDEX_NONE)
				{
					const FVector ArmDirection = Component.TransformVectorNoScale(TargetPose[Child].GetTranslation()).GetSafeNormal();
					const FVector RestDirection = FVector(Bone == TEXT("upperarm_l") ? 0.15f : -0.15f, 0.0f, -1.0f).GetSafeNormal();
					Component.SetRotation(FQuat::FindBetweenNormals(ArmDirection, RestDirection) * Component.GetRotation());
					TargetPose[Index].SetRotation((Component.GetRelativeTransform(ComponentPose[Parent])).GetRotation());
				}
			}
			ComponentPose[Index] = Component;
		}
	}
	Mesh->SetAnimInstanceClass(UPatientBedAnimInstance::StaticClass());
	PoseDriver = Cast<UPatientBedAnimInstance>(Mesh->GetAnimInstance());
	if (!PoseDriver) return;
	PoseDriver->SetTargetPose(TargetPose);
	Mesh->TickAnimation(0.0f, false);
	Mesh->RefreshBoneTransforms();
	// Initialize above the authored mattress instead of asking depenetration to
	// eject an already-overlapping patient. This runs only at bed initialization.
	float Lift = 0.0f;
	const FTransform BedTransform = BedActor->GetActorTransform();
	for (USkeletalBodySetup* Setup : Mesh->GetPhysicsAsset()->SkeletalBodySetups)
	{
		const FTransform BoneTransform = Mesh->GetSocketTransform(Setup->BoneName);
		if (!IsInsideMattress(BoneTransform.GetLocation())) continue;
		const FBox Bounds = Setup->AggGeom.CalcAABB(BoneTransform);
		const float Bottom = BedTransform.InverseTransformPosition(FVector(Bounds.GetCenter().X, Bounds.GetCenter().Y, Bounds.Min.Z)).Z;
		Lift = FMath::Max(Lift, MattressMax.Z + 1.0f - Bottom);
	}
	if (Lift > 0.0f && Lift < 100.0f)
	{
		Mesh->AddWorldOffset(BedTransform.TransformVector(FVector(0.0f, 0.0f, Lift)), false, nullptr, ETeleportType::TeleportPhysics);
		Mesh->RefreshBoneTransforms();
	}
	Mesh->TickAnimation(0.0f, false);
	Mesh->RefreshBoneTransforms();
	// Bodies were registered before animation evaluation. Recreate them from the
	// evaluated bed pose so the first physics frame never starts from stale bones.
	Mesh->RecreatePhysicsState();
	Patient->GetPatientPhysicsComponent()->ApplyMassDistribution();
	Patient->GetPatientPhysicsComponent()->ApplyBodyDamping();
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh->SetAllBodiesSimulatePhysics(true);
	Mesh->SetAllBodiesPhysicsBlendWeight(1.0f, false);
	Mesh->WakeAllRigidBodies();
	RestingBodyRotations.Empty();
	for (EPatientBoneRole Role : { EPatientBoneRole::Pelvis, EPatientBoneRole::Spine05 })
	{
		const FName Bone = Patient->GetPatientPhysicsComponent()->ResolveBoneName(Role);
		RestingBodyRotations.Add(Bone, Mesh->GetBoneQuaternion(Bone));
	}
	PelvisAnchor = Patient->GetPelvisLocation();
	const FName Chest = Patient->GetPatientPhysicsComponent()->ResolveBoneName(EPatientBoneRole::Spine05);
	SupportedTorsoDirection = (Mesh->GetBoneLocation(Chest) - PelvisAnchor).GetSafeNormal();
	bInitialized = true;
	bSuspended = false;
	UpdatePoseAndMuscles(0.0f);
	UE_LOG(LogTemp, Display, TEXT("PatientBed: Physical bed control initialized; bed=%s."), *GetNameSafe(BedActor));
}

void UPatientBedSupportComponent::SetGrabBones(const TArray<FName>& Bones)
{
	bool bPreviouslyHeld = false;
	for (FName Bone : GrabBones) bPreviouslyHeld |= IsTorsoGrab(Bone);
	GrabBones = Bones;
	for (FName Bone : Bones)
	{
		if (IsTorsoGrab(Bone))
		{
			bHasTorsoSupport = true;
			if (!bPreviouslyHeld && Patient) PelvisAnchor = Patient->GetPelvisLocation();
		}
	}
}

void UPatientBedSupportComponent::MoveSupportWithHand(FName Bone, const FVector& HandDelta)
{
	if (!IsBedControlActive() || !IsTorsoGrab(Bone)) return;
	int32 Count = 0;
	const FName Pelvis = Patient->GetPatientPhysicsComponent()->ResolveBoneName(EPatientBoneRole::Pelvis);
	const bool bHipHeld = GrabBones.Contains(Pelvis);
	if (bHipHeld && Bone != Pelvis) return; // chest travel during folding is rotation, not hip translation
	for (FName HeldBone : GrabBones) if (IsTorsoGrab(HeldBone) && (!bHipHeld || HeldBone == Pelvis)) ++Count;
	// Combine hands without doubling translation. Support follows nurse intent,
	// not pelvis drift caused by dangling legs or contact impulses.
	FVector Delta = HandDelta / FMath::Max(Count, 1);
	Delta.Z = 0.0f;
	PelvisAnchor += Delta.GetClampedToMaxSize(5.0f);
	FVector Local = BedActor->GetActorTransform().InverseTransformPosition(PelvisAnchor);
	Local.X = FMath::Clamp(Local.X, MattressMin.X + 12.0f, MattressMax.X - 12.0f);
	Local.Y = FMath::Clamp(Local.Y, MattressMin.Y + 12.0f, MattressMax.Y - 12.0f);
	PelvisAnchor = BedActor->GetActorTransform().TransformPosition(Local);
}

FName UPatientBedSupportComponent::RegionForBone(FName Bone) const
{
	if (!Patient) return NAME_None;
	USkeletalMeshComponent* Mesh = Patient->GetPatientMesh();
	UPatientPhysicsComponent* Physics = Patient->GetPatientPhysicsComponent();
	const EPatientBoneRole Roots[] = { EPatientBoneRole::ClavicleLeft, EPatientBoneRole::ClavicleRight,
		EPatientBoneRole::ThighLeft, EPatientBoneRole::ThighRight, EPatientBoneRole::Neck01,
		EPatientBoneRole::Spine01, EPatientBoneRole::Pelvis };
	for (EPatientBoneRole Role : Roots)
	{
		FName Root = Physics->ResolveBoneName(Role);
		if (!Root.IsNone() && (Bone == Root || Mesh->BoneIsChildOf(Bone, Root))) return Root;
	}
	return NAME_None;
}

bool UPatientBedSupportComponent::IsTorsoGrab(FName Bone) const
{
	if (!Patient) return false;
	FName Region = RegionForBone(Bone);
	UPatientPhysicsComponent* Physics = Patient->GetPatientPhysicsComponent();
	return Region == Physics->ResolveBoneName(EPatientBoneRole::Spine01)
		|| Region == Physics->ResolveBoneName(EPatientBoneRole::Pelvis);
}

float UPatientBedSupportComponent::GetRegionRelaxation(FName Bone) const
{
	const float* Alpha = RegionRelaxation.Find(RegionForBone(Bone));
	return Alpha ? *Alpha : 0.0f;
}

float UPatientBedSupportComponent::GetRegionMass(FName Bone) const
{
	if (!Patient) return 0.0f;
	USkeletalMeshComponent* Mesh = Patient->GetPatientMesh();
	if (!Mesh || !Mesh->GetPhysicsAsset()) return 0.0f;
	const FName Region = RegionForBone(Bone);
	float Mass = 0.0f;
	for (USkeletalBodySetup* Setup : Mesh->GetPhysicsAsset()->SkeletalBodySetups)
		if (RegionForBone(Setup->BoneName) == Region)
			if (FBodyInstance* Body = Mesh->GetBodyInstance(Setup->BoneName)) Mass += Body->GetBodyMass();
	return Mass;
}

bool UPatientBedSupportComponent::IsInsideMattress(const FVector& Location, float Margin) const
{
	if (!BedActor) return false;
	FVector Local = BedActor->GetActorTransform().InverseTransformPosition(Location);
	return Local.X >= MattressMin.X - Margin && Local.X <= MattressMax.X + Margin
		&& Local.Y >= MattressMin.Y - Margin && Local.Y <= MattressMax.Y + Margin;
}

FVector UPatientBedSupportComponent::ConstrainHandTarget(FName Bone, const FVector& Target, const FVector& CurrentContact) const
{
	if (!BedActor || !Patient) return Target;
	USkeletalMeshComponent* Mesh = Patient->GetPatientMesh();
	FBodyInstance* Body = Mesh->GetBodyInstance(Bone);
	UPhysicsAsset* Asset = Mesh->GetPhysicsAsset();
	const int32 Index = Asset ? Asset->FindBodyIndex(Bone) : INDEX_NONE;
	if (!Body || Index == INDEX_NONE) return Target;
	const FTransform Bed = BedActor->GetActorTransform();
	FTransform Predicted = Body->GetUnrealWorldTransform();
	Predicted.AddToTranslation(Target - CurrentContact);
	const FBox Bounds = Asset->SkeletalBodySetups[Index]->AggGeom.CalcAABB(Predicted.GetRelativeTransform(Bed));
	// Clamp the contacted body's underside, rather than just the hand point.
	// A calf grabbed at the ankle can otherwise be driven into the top/corner
	// while its contact point is already outside the mattress.
	const bool bOverlapsSurface = Bounds.Max.X >= MattressMin.X && Bounds.Min.X <= MattressMax.X
		&& Bounds.Max.Y >= MattressMin.Y && Bounds.Min.Y <= MattressMax.Y;
	if (!bOverlapsSurface || Bounds.Min.Z >= MattressMax.Z + 0.5f) return Target;
	FVector Local = Bed.InverseTransformPosition(Target);
	Local.Z += MattressMax.Z + 0.5f - Bounds.Min.Z;
	return BedActor->GetActorTransform().TransformPosition(Local);
}

void UPatientBedSupportComponent::UpdatePoseAndMuscles(float DeltaTime)
{
	USkeletalMeshComponent* Mesh = Patient->GetPatientMesh();
	TSet<FName> HeldRegions;
	for (FName Bone : GrabBones) HeldRegions.Add(RegionForBone(Bone));
	for (USkeletalBodySetup* Setup : Mesh->GetPhysicsAsset()->SkeletalBodySetups)
		RegionRelaxation.FindOrAdd(RegionForBone(Setup->BoneName));
	for (auto& Pair : RegionRelaxation)
	{
		Pair.Value = HeldRegions.Contains(Pair.Key) ? 1.0f
			: FMath::Max(0.0f, Pair.Value - DeltaTime / FMath::Max(MuscleRecoveryTime, 0.01f));
	}
	const FReferenceSkeleton& Ref = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
	FPoseSnapshot Snapshot;
	Mesh->SnapshotPose(Snapshot);
	for (int32 I = 0; I < Snapshot.BoneNames.Num(); ++I)
	{
		FName Bone = Snapshot.BoneNames[I];
		FName Region = RegionForBone(Bone);
		float Alpha = RegionRelaxation.FindRef(Region);
		int32 Index = Ref.FindBoneIndex(Bone);
		if (!TargetPose.IsValidIndex(Index)) continue;
		// Follow the articulated pose while touched. Root/pelvis always follow the body;
		// this prevents world-space motors from pulling back toward the original bed position.
		if (Index == 0 || Bone == Patient->GetPatientPhysicsComponent()->ResolveBoneName(EPatientBoneRole::Pelvis)
			|| Alpha > 0.0f)
		{
			TargetPose[Index].SetRotation(Snapshot.LocalTransforms[I].GetRotation());
		}
		if (Index == 0) TargetPose[Index].SetTranslation(Snapshot.LocalTransforms[I].GetTranslation());
	}
	PoseDriver->SetTargetPose(TargetPose);
	UPhysicalAnimationComponent* Motors = Patient->GetPhysicalAnimationComponent();
	for (USkeletalBodySetup* Setup : Mesh->GetPhysicsAsset()->SkeletalBodySetups)
	{
		FName Bone = Setup->BoneName;
		const bool bTorso = IsTorsoGrab(Bone);
		const bool bNeck = RegionForBone(Bone) == Patient->GetPatientPhysicsComponent()->ResolveBoneName(EPatientBoneRole::Neck01);
		float Alpha = GetRegionRelaxation(Bone);
		FPhysicalAnimationData Data;
		Data.bIsLocalSimulation = true;
		Data.OrientationStrength = (bTorso ? 90.0f : bNeck ? 55.0f : 4.0f) * (1.0f - 0.9f * Alpha);
		Data.AngularVelocityStrength = bTorso ? 35.0f : 15.0f;
		Data.MaxAngularForce = bTorso ? 1800.0f : bNeck ? 600.0f : 150.0f;
		Data.MaxLinearForce = 1.0f;
		Motors->ApplyPhysicalAnimationSettings(Bone, Data);
	}
}

void UPatientBedSupportComponent::UpdateSupport(float DeltaTime)
{
	USkeletalMeshComponent* Mesh = Patient->GetPatientMesh();
	UPatientPhysicsComponent* Physics = Patient->GetPatientPhysicsComponent();
	const FName Pelvis = Physics->ResolveBoneName(EPatientBoneRole::Pelvis);
	FBodyInstance* Body = Mesh->GetBodyInstance(Pelvis);
	if (!Body) return;
	const FVector Position = Patient->GetPelvisLocation();
	if (!bHasTorsoSupport && IsInsideMattress(Position))
	{
		// Weak supine balance before the nurse begins torso/hip guidance. There is
		// no lift or position pin; mattress contact bears the patient's weight.
		for (const auto& Rest : RestingBodyRotations)
		{
			FBodyInstance* RestBody = Mesh->GetBodyInstance(Rest.Key);
			if (!RestBody) continue;
			FQuat Error = Rest.Value * RestBody->GetUnrealWorldTransform().GetRotation().Inverse();
			if (Error.W < 0.0f) Error = Error * -1.0f;
			FVector Axis; float Angle;
			Error.ToAxisAndAngle(Axis, Angle);
			const float Stiffness = Rest.Key == Pelvis ? 45000.0f : 10000.0f;
			const FVector Torque = Axis * Angle * Stiffness - RestBody->GetUnrealWorldAngularVelocityInRadians() * 1500.0f;
			RestBody->AddTorqueInRadians(Torque.GetClampedToMaxSize(75000.0f) * RestBody->GetBodyMass(), false, false);
		}
	}
	bool bTorsoHeld = false;
	for (FName Bone : GrabBones) bTorsoHeld |= IsTorsoGrab(Bone);
	if (bHasTorsoSupport && IsInsideMattress(Position, 12.0f))
	{
		FVector Local = BedActor->GetActorTransform().InverseTransformPosition(PelvisAnchor);
		// Retain calibrated initial hip clearance, and let contact support its weight.
		Local.Z = FMath::Max(Local.Z, MattressMax.Z + 7.0f);
		FVector Target = BedActor->GetActorTransform().TransformPosition(Local);
		FVector Error = Target - Position;
		FVector Acceleration = Error * SupportStiffness - Body->GetUnrealWorldVelocity() * SupportDamping;
		Acceleration.Z += 650.0f; // partial weight support, never full kinematic pinning
		// The hips bear the torso and dangling legs. Size the compliant surface
		// support for that load without changing any body's inertial mass.
		const float SupportedMass = FMath::Max(Body->GetBodyMass(), Mesh->GetMass() * 0.65f);
		Body->AddForce(Acceleration.GetClampedToMaxSize(1600.0f) * SupportedMass, false, false);
	}
	else PelvisAnchor = Position;

	const FName ChestBone = Physics->ResolveBoneName(EPatientBoneRole::Spine05);
	FVector Torso = (Mesh->GetBoneLocation(ChestBone) - Position).GetSafeNormal();
	if (bTorsoHeld) SupportedTorsoDirection = Torso;
	if (!bHasTorsoSupport || !IsInsideMattress(Position, 12.0f)) return;
	if (FVector::DotProduct(Torso, FVector::UpVector) > 0.3f)
		for (USkeletalBodySetup* Setup : Mesh->GetPhysicsAsset()->SkeletalBodySetups)
		{
			const FName Region = RegionForBone(Setup->BoneName);
			if (!IsTorsoGrab(Setup->BoneName) && Region != Physics->ResolveBoneName(EPatientBoneRole::Neck01)) continue;
			if (Setup->BoneName == Pelvis) continue;
			if (FBodyInstance* SupportedBody = Mesh->GetBodyInstance(Setup->BoneName))
				SupportedBody->AddForce(FVector(0, 0, 850.0f * SupportedBody->GetBodyMass()), false, false);
		}
	FName ChestBody = Mesh->FindClosestBone(Mesh->GetBoneLocation(ChestBone), nullptr, 0.0f, true);
	FBodyInstance* Chest = Mesh->GetBodyInstance(ChestBody);
	if (!Chest) return;
	FVector Desired = bStableEdgeSeated ? FVector::UpVector : SupportedTorsoDirection;
	FVector Torque = FVector::CrossProduct(Torso, Desired) * BalanceStiffness
		- Chest->GetUnrealWorldAngularVelocityInRadians() * 1300.0f;
	Torque *= Chest->GetBodyMass();
	if (FVector::DotProduct(Torso, FVector::UpVector) > 0.3f)
		for (USkeletalBodySetup* Setup : Mesh->GetPhysicsAsset()->SkeletalBodySetups)
		{
			const FName Bone = Setup->BoneName;
			const FName SpineRoot = Physics->ResolveBoneName(EPatientBoneRole::Spine01);
			if (Bone != SpineRoot && !Mesh->BoneIsChildOf(Bone, SpineRoot)) continue;
			FBodyInstance* UpperBody = Mesh->GetBodyInstance(Bone);
			if (!UpperBody) continue;
			const bool bWeightSupported = IsTorsoGrab(Bone)
				|| RegionForBone(Bone) == Physics->ResolveBoneName(EPatientBoneRole::Neck01);
			const FVector Gravity(0, 0, -(bWeightSupported ? 130.0f : 980.0f) * UpperBody->GetBodyMass());
			// Balance the current upper-body load around the hips. This cancels most
			// gravitational bending without increasing the posture spring or fixing
			// a pose; arm movement still changes the balance load naturally.
			Torque -= FVector::CrossProduct(UpperBody->GetCOMPosition() - Position, Gravity) * 0.85f;
		}
	Chest->AddTorqueInRadians(Torque.GetClampedToMaxSize(75000.0f * Chest->GetBodyMass()), false, false);
	// Cooperation is activated by torso/hip support, not by lifting a hand or head.
	if (bTorsoHeld && FVector::DotProduct(Torso, FVector::UpVector) > 0.15f)
		Chest->AddForce(FVector(0.0f, 0.0f, 400.0f * Chest->GetBodyMass()), false, false);
}

void UPatientBedSupportComponent::UpdateSeated(float DeltaTime)
{
	UPatientPhysicsComponent* Physics = Patient->GetPatientPhysicsComponent();
	USkeletalMeshComponent* Mesh = Patient->GetPatientMesh();
	const FTransform BedTransform = BedActor->GetActorTransform();
	const FVector LocalPelvis = BedTransform.InverseTransformPosition(Patient->GetPelvisLocation());
	const float EdgeDistance = FMath::Abs(FVector::DotProduct(LocalPelvis - EdgeMarker, EdgeOutward.GetSafeNormal()));
	bool bLegsClear = true;
	for (EPatientBoneRole Role : { EPatientBoneRole::FootLeft, EPatientBoneRole::FootRight })
	{
		const FName Bone = Physics->ResolveBoneName(Role);
		if (Bone.IsNone() || Mesh->GetBoneIndex(Bone) == INDEX_NONE) { bLegsClear = false; break; }
		const FVector Local = BedTransform.InverseTransformPosition(Mesh->GetBoneLocation(Bone));
		bLegsClear &= FVector::DotProduct(Local - EdgeMarker, EdgeOutward.GetSafeNormal()) > 4.0f
			&& Local.Z < MattressMax.Z;
	}
	const bool bCandidate = bHasTorsoSupport && IsInsideMattress(Patient->GetPelvisLocation(), 12.0f)
		&& LocalPelvis.Z >= MattressMax.Z - 5.0f && LocalPelvis.Z <= MattressMax.Z + 35.0f
		&& EdgeDistance <= EdgeTolerance && bLegsClear
		&& Patient->GetSeatedTransitionComponent()->GetTorsoUprightAngleDeg()
			<= Patient->GetSeatedTransitionComponent()->SitUprightAngleThreshold
		&& Patient->GetPelvisVelocity().Size() <= SettleSpeed;
	StableTime = bCandidate ? StableTime + DeltaTime : 0.0f;
	bool bWasStable = bStableEdgeSeated;
	bStableEdgeSeated = StableTime >= RequiredStableTime;
	if (bStableEdgeSeated && !bWasStable)
	{
		Patient->NotifyBedSeated();
		UE_LOG(LogTemp, Display, TEXT("PatientBed: Stable edge seating reached with physics and grabs retained."));
	}
}

void UPatientBedSupportComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(DeltaTime, TickType, TickFunction);
	if (!IsBedControlActive() || !Patient || Patient->IsSpineDamaged() || !PoseDriver) return;
	UpdatePoseAndMuscles(DeltaTime);
	UpdateSupport(DeltaTime);
	UpdateSeated(DeltaTime);
}

void UPatientBedSupportComponent::SuspendForTransfer()
{
	bSuspended = true;
	GrabBones.Empty();
	RegionRelaxation.Empty();
}

void UPatientBedSupportComponent::ResumeOnBed()
{
	bInitialized = false;
	bSuspended = false;
	bStableEdgeSeated = false;
	StableTime = 0.0f;
	Initialize(Patient);
}
