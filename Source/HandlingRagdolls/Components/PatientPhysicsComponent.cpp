// Fill out your copyright notice in the Description page of Project Settings.

#include "PatientPhysicsComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "PhysicsEngine/PhysicalAnimationComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"
#include "../Patient/PatientTypes.h"
#include "../Patient/PatientBoneMapping.h"

UPatientPhysicsComponent::UPatientPhysicsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Run AFTER physics so our held-pose re-assertion is the final word each frame.
	// Otherwise the skeletal mesh syncs kinematic bodies to the (lying) anim pose
	// after our tick, overwriting the hold and snapping the spine back down.
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UPatientPhysicsComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UPatientPhysicsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// --- HOLD POSE: re-assert constrained (Anchored) bodies after physics. ---
	// World constraints prevent movement during the physics step while keeping the
	// bodies simulated, so the physical pose continues to drive the rendered mesh.
	if (bHoldingPose && Mesh)
	{
		for (const TPair<FName, FTransform>& Pair : HeldBoneTransforms)
		{
			FBodyInstance* BI = Mesh->GetBodyInstance(Pair.Key);
			if (BI)
			{
				BI->SetBodyTransform(Pair.Value, ETeleportType::TeleportPhysics, false);
				BI->SetLinearVelocity(FVector::ZeroVector, false);
				BI->SetAngularVelocityInRadians(FVector::ZeroVector, false);
			}
		}
	}

	// --- PIVOT POSE: position-pinned + pitch/roll locked, but yaw driven externally ---
	// Each pivot bone holds its captured XYZ position and pitch/roll, but its yaw
	// is overridden by PivotTargetYaw (relative to PivotBaseYaw).
	if (PivotBoneTransforms.Num() > 0 && Mesh)
	{
		const float YawDelta = PivotTargetYaw - PivotBaseYaw;
		const FQuat YawRotation = FQuat(FVector::UpVector, FMath::DegreesToRadians(YawDelta));

		for (const TPair<FName, FTransform>& Pair : PivotBoneTransforms)
		{
			FBodyInstance* BI = Mesh->GetBodyInstance(Pair.Key);
			if (BI)
			{
				// Take the original captured rotation and apply yaw delta to it
				FTransform PivotedTransform = Pair.Value;
				FQuat OrigRotation = PivotedTransform.GetRotation();
				FQuat NewRotation = YawRotation * OrigRotation;
				PivotedTransform.SetRotation(NewRotation);

				BI->SetBodyTransform(PivotedTransform, ETeleportType::TeleportPhysics, false);
				BI->SetLinearVelocity(FVector::ZeroVector, false);
				BI->SetAngularVelocityInRadians(FVector::ZeroVector, false);
			}
		}
	}

	if (!bDiagLogging || !Mesh) return;

	DiagElapsed += DeltaTime;
	DiagFrame++;

	// Log spine/pelvis/neck positions and sim state each frame for ~1 second
	FName Pelvis = ResolveBoneName(EPatientBoneRole::Pelvis);
	FName Spine = ResolveBoneName(EPatientBoneRole::Spine03);
	FName Spine5 = ResolveBoneName(EPatientBoneRole::Spine05);
	FName Neck = ResolveBoneName(EPatientBoneRole::Neck01);

	auto BoneInfo = [this](FName B) -> FString
	{
		if (B.IsNone()) return TEXT("<none>");
		FBodyInstance* BI = Mesh->GetBodyInstance(B);
		FVector Loc = Mesh->GetBoneLocation(B);
		bool bSim = BI ? BI->IsInstanceSimulatingPhysics() : false;
		return FString::Printf(TEXT("%s=(%.1f,%.1f,%.1f) sim=%d"),
			*B.ToString(), Loc.X, Loc.Y, Loc.Z, bSim ? 1 : 0);
	};

	// Torso angle
	float Angle = 90.0f;
	if (!Pelvis.IsNone() && !Spine5.IsNone())
	{
		FVector Dir = Mesh->GetBoneLocation(Spine5) - Mesh->GetBoneLocation(Pelvis);
		if (Dir.Normalize())
		{
			Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Dir, FVector::UpVector), -1.f, 1.f)));
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[DIAG f%d t%.2f] angle=%.1f | %s | %s | %s | %s"),
		DiagFrame, DiagElapsed, Angle,
		*BoneInfo(Pelvis), *BoneInfo(Spine), *BoneInfo(Spine5), *BoneInfo(Neck));

	if (DiagElapsed >= 1.5f)
	{
		bDiagLogging = false;
		UE_LOG(LogTemp, Warning, TEXT("[DIAG] --- logging ended ---"));
	}
}

void UPatientPhysicsComponent::Initialize(USkeletalMeshComponent* InMesh, UPhysicalAnimationComponent* InPhysAnim,
	USpineConstraintConfig* InConfig, UPatientBoneMapping* InBoneMapping)
{
	Mesh = InMesh;
	PhysicalAnimation = InPhysAnim;
	SpineConfig = InConfig;
	BoneMapping = InBoneMapping;

	if (PhysicalAnimation && Mesh)
	{
		PhysicalAnimation->SetSkeletalMeshComponent(Mesh);
	}
}

// ============================================================
// Physics Mode Control
// ============================================================

void UPatientPhysicsComponent::EnablePhysicalAnimation()
{
	if (!Mesh) return;

	// Resolve the root bone for simulation (pelvis)
	FName RootBone = ResolveBoneName(EPatientBoneRole::Pelvis);

	// Apply the physical animation motor settings FIRST so they're in place
	// before the bodies start simulating.
	ApplyProfile(ActiveProfile);

	if (RootBone.IsNone())
	{
		// Fallback: simulate all bodies if we can't resolve the root.
		Mesh->SetAllBodiesSimulatePhysics(true);
	}
	else
	{
		// Simulate everything BELOW the pelvis, but leave the pelvis itself
		// KINEMATIC (bIncludeSelf = false). The pelvis stays animation-driven and
		// fixed in place, acting as an anchor.
		Mesh->SetAllBodiesBelowSimulatePhysics(RootBone, true, /*bIncludeSelf=*/false);
	}

	Mesh->WakeAllRigidBodies();
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
}

void UPatientPhysicsComponent::EnableRagdoll()
{
	if (!Mesh) return;

	// Full limp ragdoll — clear any physical animation motor drive.
	if (PhysicalAnimation)
	{
		FName RootBone = ResolveBoneName(EPatientBoneRole::Pelvis);
		if (!RootBone.IsNone())
		{
			PhysicalAnimation->ApplyPhysicalAnimationSettingsBelow(RootBone, FPhysicalAnimationData());
		}
	}

	Mesh->SetSimulatePhysics(true);
	Mesh->SetAllBodiesSimulatePhysics(true);
	Mesh->WakeAllRigidBodies();
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

	ActiveProfile = EPhysicalAnimProfile::Limp;
}

void UPatientPhysicsComponent::ApplyProfile(EPhysicalAnimProfile Profile)
{
	if (!PhysicalAnimation || !Mesh) return;

	ActiveProfile = Profile;

	FName DefaultRootBone = ResolveBoneName(EPatientBoneRole::Pelvis);

	// Limp profile = no motor drive (pure ragdoll behavior).
	if (Profile == EPhysicalAnimProfile::Limp)
	{
		if (!DefaultRootBone.IsNone())
		{
			PhysicalAnimation->ApplyPhysicalAnimationSettingsBelow(DefaultRootBone, FPhysicalAnimationData());
		}
		return;
	}

	// Look up the designer-configured profile data.
	const FPhysicalAnimProfileData* ProfileData = FindProfileData(Profile);

	FPhysicalAnimationData AnimData;
	FName DriveBelowBone = DefaultRootBone;

	if (ProfileData)
	{
		AnimData.bIsLocalSimulation = ProfileData->bIsLocalSimulation;
		AnimData.OrientationStrength = ProfileData->OrientationStrength;
		AnimData.AngularVelocityStrength = ProfileData->AngularVelocityStrength;
		AnimData.PositionStrength = ProfileData->PositionStrength;
		AnimData.VelocityStrength = ProfileData->VelocityStrength;
		AnimData.MaxLinearForce = ProfileData->MaxLinearForce;
		AnimData.MaxAngularForce = ProfileData->MaxAngularForce;
		if (!ProfileData->DriveBelowBone.IsNone())
		{
			DriveBelowBone = ProfileData->DriveBelowBone;
		}
	}
	else
	{
		// Fallback defaults tuned per profile.
		switch (Profile)
		{
		case EPhysicalAnimProfile::Relaxed:
			AnimData.bIsLocalSimulation = false;
			AnimData.OrientationStrength = 350.0f;
			AnimData.AngularVelocityStrength = 200.0f;
			AnimData.PositionStrength = 0.0f;
			AnimData.VelocityStrength = 0.0f;
			AnimData.MaxAngularForce = 350.0f;
			AnimData.MaxLinearForce = 0.0f;
			break;
		case EPhysicalAnimProfile::Cooperating:
			AnimData.bIsLocalSimulation = false;
			AnimData.OrientationStrength = 1200.0f;
			AnimData.AngularVelocityStrength = 120.0f;
			AnimData.PositionStrength = 200.0f;
			AnimData.VelocityStrength = 50.0f;
			AnimData.MaxAngularForce = 1000.0f;
			AnimData.MaxLinearForce = 500.0f;
			break;
		case EPhysicalAnimProfile::Seated:
			AnimData.bIsLocalSimulation = false;
			AnimData.OrientationStrength = 2000.0f;
			AnimData.AngularVelocityStrength = 200.0f;
			AnimData.PositionStrength = 400.0f;
			AnimData.VelocityStrength = 100.0f;
			AnimData.MaxAngularForce = 1500.0f;
			AnimData.MaxLinearForce = 800.0f;
			break;
		default:
			break;
		}
	}

	if (!DriveBelowBone.IsNone())
	{
		PhysicalAnimation->ApplyPhysicalAnimationSettingsBelow(DriveBelowBone, AnimData);
	}
}

void UPatientPhysicsComponent::ApplyStateConfig(UPatientStateConfig* Config)
{
	if (!Config || !Mesh || !PhysicalAnimation) return;

	USkeletalMesh* SkelAsset = Mesh->GetSkeletalMeshAsset();
	if (!SkelAsset) return;
	const FReferenceSkeleton& RefSkel = SkelAsset->GetRefSkeleton();

	// --- Capture CURRENT world transforms of all bodies BEFORE changing anything ---
	// This is the pose the player folded the patient into. Anchored bones will be
	// pinned to these transforms every frame so they HOLD this pose.
	TMap<FName, FTransform> PreTransforms;
	TArray<FName> AllBones;
	Mesh->GetBoneNames(AllBones);
	for (const FName& B : AllBones)
	{
		FBodyInstance* BI = Mesh->GetBodyInstance(B);
		if (BI)
		{
			PreTransforms.Add(B, BI->GetUnrealWorldTransform());
		}
	}
	ClearAnchorConstraints();

	// --- Build a complete behavior map (default for all, then overrides) ---
	static const TArray<EPatientBoneGroup> OrderedGroups = {
		EPatientBoneGroup::WholeBodyBelowPelvis,
		EPatientBoneGroup::Pelvis,
		EPatientBoneGroup::Spine,
		EPatientBoneGroup::Neck,
		EPatientBoneGroup::Head,
		EPatientBoneGroup::LeftArm,
		EPatientBoneGroup::RightArm,
		EPatientBoneGroup::LeftLeg,
		EPatientBoneGroup::RightLeg
	};

	TMap<EPatientBoneGroup, EBoneBehavior> GroupBehaviors;
	TMap<EPatientBoneGroup, float> GroupStrengthOverrides;
	for (EPatientBoneGroup G : OrderedGroups)
	{
		GroupBehaviors.Add(G, Config->DefaultBehavior);
		GroupStrengthOverrides.Add(G, -1.0f);
	}
	for (const FBoneGroupBehavior& GB : Config->BoneGroups)
	{
		GroupBehaviors[GB.Group] = GB.Behavior;
		GroupStrengthOverrides[GB.Group] = GB.OrientationStrengthOverride;
	}

	// Precompute each group's subtree-root bone INDEX (for narrowest-match resolution)
	TMap<EPatientBoneGroup, int32> GroupRootIndex;
	for (EPatientBoneGroup G : OrderedGroups)
	{
		FName Root = ResolveGroupRoot(G);
		GroupRootIndex.Add(G, Root.IsNone() ? INDEX_NONE : RefSkel.FindBoneIndex(Root));
	}
	const int32 PelvisIdx = RefSkel.FindBoneIndex(ResolveBoneName(EPatientBoneRole::Pelvis));

	// Ancestor-or-self check via the reference skeleton hierarchy.
	auto IsAncestorOrSelf = [&RefSkel](int32 AncestorIdx, int32 BoneIdx) -> bool
	{
		if (AncestorIdx == INDEX_NONE) return false;
		int32 Cur = BoneIdx;
		while (Cur != INDEX_NONE)
		{
			if (Cur == AncestorIdx) return true;
			Cur = RefSkel.GetParentIndex(Cur);
		}
		return false;
	};

	// Get hierarchy depth (hops from root). Deeper = narrower group.
	auto GetBoneDepth = [&RefSkel](int32 BoneIdx) -> int32
	{
		int32 Depth = 0;
		int32 Cur = BoneIdx;
		while (Cur != INDEX_NONE)
		{
			Cur = RefSkel.GetParentIndex(Cur);
			Depth++;
		}
		return Depth;
	};

	// Resolve the controlling behavior for a bone = the NARROWEST (deepest-rooted)
	// group whose subtree contains it.
	auto ResolveBoneBehavior = [&](int32 BoneIdx, float& OutStrength) -> EBoneBehavior
	{
		// Pelvis is special: controlled only by the Pelvis group.
		if (BoneIdx == PelvisIdx)
		{
			OutStrength = GroupStrengthOverrides[EPatientBoneGroup::Pelvis];
			return GroupBehaviors[EPatientBoneGroup::Pelvis];
		}

		EBoneBehavior Beh = GroupBehaviors[EPatientBoneGroup::WholeBodyBelowPelvis];
		OutStrength = GroupStrengthOverrides[EPatientBoneGroup::WholeBodyBelowPelvis];
		int32 BestDepth = -1;

		static const TArray<EPatientBoneGroup> NarrowGroups = {
			EPatientBoneGroup::Spine, EPatientBoneGroup::Neck, EPatientBoneGroup::Head,
			EPatientBoneGroup::LeftArm, EPatientBoneGroup::RightArm,
			EPatientBoneGroup::LeftLeg, EPatientBoneGroup::RightLeg
		};
		for (EPatientBoneGroup G : NarrowGroups)
		{
			const int32 RootIdx = GroupRootIndex[G];
			if (RootIdx == INDEX_NONE) continue;
			if (IsAncestorOrSelf(RootIdx, BoneIdx))
			{
				int32 Depth = GetBoneDepth(RootIdx);
				if (Depth > BestDepth)
				{
					BestDepth = Depth;
					Beh = GroupBehaviors[G];
					OutStrength = GroupStrengthOverrides[G];
				}
			}
		}
		return Beh;
	};

	// --- Apply per-bone. All bodies simulate so physics drives the rendered pose. ---
	// Anchored bodies receive world constraints and a PostPhysics transform hold.
	HeldBoneTransforms.Empty();
	PivotBoneTransforms.Empty();

	// State anchors do not use animation-driven kinematic bodies.
	Mesh->bUpdateMeshWhenKinematic = false;
	Mesh->KinematicBonesUpdateType = EKinematicBonesUpdateToPhysics::SkipSimulatingBones;

	for (const FName& BoneName : AllBones)
	{
		FBodyInstance* BI = Mesh->GetBodyInstance(BoneName);
		if (!BI) continue;

		const int32 BoneIdx = RefSkel.FindBoneIndex(BoneName);
		float StrengthOverride = -1.0f;
		const EBoneBehavior Beh = ResolveBoneBehavior(BoneIdx, StrengthOverride);

		// Every behavior starts from a deterministic simulated state.
		BI->SetInstanceSimulatePhysics(true, false, true);

		FPhysicalAnimationData Data;
		Data.bIsLocalSimulation = false;

		switch (Beh)
		{
		case EBoneBehavior::Anchored:
		{
			// The pelvis is the root anchor: make it genuinely kinematic so no grab
			// force can translate the patient. Other anchored bodies stay simulated
			// and use world constraints so they continue to render the captured pose.
			const FTransform* Captured = PreTransforms.Find(BoneName);
			if (Captured)
			{
				HeldBoneTransforms.Add(BoneName, *Captured);
				const bool bIsPelvisAnchor = (BoneIdx == PelvisIdx);
				if (bIsPelvisAnchor)
				{
					BI->SetInstanceSimulatePhysics(false, false, true);
				}
				BI->SetBodyTransform(*Captured, ETeleportType::TeleportPhysics, false);
				BI->SetLinearVelocity(FVector::ZeroVector, false);
				BI->SetAngularVelocityInRadians(FVector::ZeroVector, false);

				if (!bIsPelvisAnchor)
				{
					UPhysicsConstraintComponent* Anchor = NewObject<UPhysicsConstraintComponent>(GetOwner());
					if (Anchor)
					{
						Anchor->RegisterComponent();
						Anchor->SetWorldTransform(*Captured);
						Anchor->SetDisableCollision(true);
						Anchor->SetLinearXLimit(ELinearConstraintMotion::LCM_Locked, 0.0f);
						Anchor->SetLinearYLimit(ELinearConstraintMotion::LCM_Locked, 0.0f);
						Anchor->SetLinearZLimit(ELinearConstraintMotion::LCM_Locked, 0.0f);
						Anchor->SetAngularSwing1Limit(EAngularConstraintMotion::ACM_Locked, 0.0f);
						Anchor->SetAngularSwing2Limit(EAngularConstraintMotion::ACM_Locked, 0.0f);
						Anchor->SetAngularTwistLimit(EAngularConstraintMotion::ACM_Locked, 0.0f);
						Anchor->SetConstrainedComponents(Mesh, BoneName, nullptr, NAME_None);
						AnchorConstraints.Add(Anchor);
					}
				}
			}
			Data.OrientationStrength = 0.0f;
			Data.AngularVelocityStrength = 0.0f;
			break;
		}
		case EBoneBehavior::Pivot:
		{
			// Position-pinned + pitch/roll locked, but yaw is free (driven externally).
			// Capture the initial transform and store in PivotBoneTransforms.
			// The tick function will re-assert position + pitch/roll and apply the
			// externally-driven PivotTargetYaw.
			const FTransform* Captured = PreTransforms.Find(BoneName);
			if (Captured)
			{
				PivotBoneTransforms.Add(BoneName, *Captured);
				BI->SetBodyTransform(*Captured, ETeleportType::TeleportPhysics, false);
				BI->SetLinearVelocity(FVector::ZeroVector, false);
				BI->SetAngularVelocityInRadians(FVector::ZeroVector, false);
			}
			Data.OrientationStrength = 0.0f;
			Data.AngularVelocityStrength = 0.0f;
			break;
		}
		case EBoneBehavior::Stiff:
			// Local-space simulation: holds each bone's orientation RELATIVE to its
			// parent (posture), rather than a world orientation. This holds the pose
			// correctly even when the actor is rotated/folded, and avoids violent
			// snapping toward a mismatched world animation pose.
			Data.bIsLocalSimulation = true;
			Data.OrientationStrength = (StrengthOverride >= 0.0f) ? StrengthOverride : Config->StiffOrientationStrength;
			Data.AngularVelocityStrength = Config->StiffAngularVelocityStrength;
			Data.MaxAngularForce = Config->StiffMaxAngularForce;
			break;
		case EBoneBehavior::Free:
			Data.bIsLocalSimulation = false;
			Data.OrientationStrength = (StrengthOverride >= 0.0f) ? StrengthOverride : Config->FreeOrientationStrength;
			Data.AngularVelocityStrength = Config->FreeAngularVelocityStrength;
			break;
		}

		// Per-bone motor settings (ApplyPhysicalAnimationSettings targets just this body).
		PhysicalAnimation->ApplyPhysicalAnimationSettings(BoneName, Data);
	}

	Mesh->WakeAllRigidBodies();
	bHoldingPose = (HeldBoneTransforms.Num() > 0);

	// Initialize pivot base yaw from the first pivot bone's current yaw
	if (PivotBoneTransforms.Num() > 0)
	{
		const FTransform& FirstPivot = PivotBoneTransforms.CreateConstIterator().Value();
		PivotBaseYaw = FirstPivot.GetRotation().Rotator().Yaw;
		PivotTargetYaw = PivotBaseYaw;
	}

	// Diagnostic: what pose did we capture for the spine?
	{
		FName SpineDiag = ResolveBoneName(EPatientBoneRole::Spine05);
		const FTransform* Held = HeldBoneTransforms.Find(SpineDiag);
		const float HeldZ = Held ? Held->GetLocation().Z : -1.0f;
		UE_LOG(LogTemp, Warning, TEXT("[SEAT] Captured hold pose: spine_05 held Z=%.1f (held count=%d)"), HeldZ, HeldBoneTransforms.Num());
	}

	const FName PelvisBone = ResolveBoneName(EPatientBoneRole::Pelvis);
	UE_LOG(LogTemp, Log, TEXT("PatientPhysics: Applied state config '%s' (default=%d, %d overrides, held=%d, pelvisAnchored=%d)"),
		*Config->GetName(), (int32)Config->DefaultBehavior, Config->BoneGroups.Num(), HeldBoneTransforms.Num(),
		HeldBoneTransforms.Contains(PelvisBone) ? 1 : 0);

	// Diagnostic logging window
	if (bHoldingPose)
	{
		bDiagLogging = true;
		DiagElapsed = 0.0f;
		DiagFrame = 0;
		UE_LOG(LogTemp, Warning, TEXT("[DIAG] --- logging started for config '%s' ---"), *Config->GetName());
	}
}

void UPatientPhysicsComponent::ClearHeldPose()
{
	ClearAnchorConstraints();
	if (Mesh)
	{
		const FName Pelvis = ResolveBoneName(EPatientBoneRole::Pelvis);
		if (HeldBoneTransforms.Contains(Pelvis))
		{
			if (FBodyInstance* PelvisBody = Mesh->GetBodyInstance(Pelvis))
			{
				PelvisBody->SetInstanceSimulatePhysics(true, false, true);
			}
		}
	}
	HeldBoneTransforms.Empty();
	PivotBoneTransforms.Empty();
	bHoldingPose = false;
	UE_LOG(LogTemp, Log, TEXT("PatientPhysics: Cleared held pose — all bones free to simulate."));
}

void UPatientPhysicsComponent::ClearAnchorConstraints()
{
	for (UPhysicsConstraintComponent* Anchor : AnchorConstraints)
	{
		if (Anchor)
		{
			Anchor->BreakConstraint();
			Anchor->DestroyComponent();
		}
	}
	AnchorConstraints.Empty();
}

void UPatientPhysicsComponent::SetPivotYaw(float NewYawDegrees)
{
	PivotTargetYaw = NewYawDegrees;
}

FName UPatientPhysicsComponent::ResolveGroupRoot(EPatientBoneGroup Group) const
{
	// Map each group to its subtree root role, then resolve to an actual bone name.
	switch (Group)
	{
	case EPatientBoneGroup::WholeBodyBelowPelvis:	return ResolveBoneName(EPatientBoneRole::Pelvis);
	case EPatientBoneGroup::Pelvis:					return ResolveBoneName(EPatientBoneRole::Pelvis);
	case EPatientBoneGroup::Spine:					return ResolveBoneName(EPatientBoneRole::Spine01);
	case EPatientBoneGroup::Neck:					return ResolveBoneName(EPatientBoneRole::Neck01);
	case EPatientBoneGroup::Head:					return ResolveBoneName(EPatientBoneRole::Head);
	case EPatientBoneGroup::LeftArm:				return ResolveBoneName(EPatientBoneRole::ClavicleLeft);
	case EPatientBoneGroup::RightArm:				return ResolveBoneName(EPatientBoneRole::ClavicleRight);
	case EPatientBoneGroup::LeftLeg:				return ResolveBoneName(EPatientBoneRole::ThighLeft);
	case EPatientBoneGroup::RightLeg:				return ResolveBoneName(EPatientBoneRole::ThighRight);
	default:										return NAME_None;
	}
}

void UPatientPhysicsComponent::ApplyCustomSettings(const FPhysicalAnimationData& Settings, FName BelowBone)
{
	if (!PhysicalAnimation) return;

	if (BelowBone.IsNone())
	{
		BelowBone = ResolveBoneName(EPatientBoneRole::Pelvis);
	}

	if (!BelowBone.IsNone())
	{
		PhysicalAnimation->ApplyPhysicalAnimationSettingsBelow(BelowBone, Settings);
	}
}

void UPatientPhysicsComponent::ApplyCustomSettingsForBone(const FPhysicalAnimationData& Settings, FName BoneName)
{
	if (!PhysicalAnimation || BoneName.IsNone()) return;
	PhysicalAnimation->ApplyPhysicalAnimationSettingsBelow(BoneName, Settings);
}

// ============================================================
// Body Configuration
// ============================================================

void UPatientPhysicsComponent::ApplyMassDistribution()
{
	if (!Mesh || !bOverrideMass) return;

	float TargetMassKg = TargetBodyMassKg;
	if (SpineConfig && SpineConfig->PatientMassKg > 0.0f)
	{
		TargetMassKg = SpineConfig->PatientMassKg;
	}

	TArray<FName> AllBoneNames;
	Mesh->GetBoneNames(AllBoneNames);

	float CurrentTotalMass = 0.0f;
	int32 BodyCount = 0;
	for (const FName& BoneName : AllBoneNames)
	{
		FBodyInstance* BodyInst = Mesh->GetBodyInstance(BoneName);
		if (BodyInst)
		{
			CurrentTotalMass += BodyInst->GetBodyMass();
			BodyCount++;
		}
	}

	if (CurrentTotalMass <= KINDA_SMALL_NUMBER || BodyCount == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("PatientPhysics: Could not read body masses (physics not initialized?)."));
		return;
	}

	const float MassScale = TargetMassKg / CurrentTotalMass;

	for (const FName& BoneName : AllBoneNames)
	{
		FBodyInstance* BodyInst = Mesh->GetBodyInstance(BoneName);
		if (BodyInst)
		{
			Mesh->SetMassScale(BoneName, MassScale);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("PatientPhysics: Scaled mass from %.1f to %.1f kg (scale %.2f) across %d bodies"),
		CurrentTotalMass, TargetMassKg, MassScale, BodyCount);
}

void UPatientPhysicsComponent::ApplyBodyDamping()
{
	if (!Mesh) return;

	TArray<FName> AllBoneNames;
	Mesh->GetBoneNames(AllBoneNames);

	for (const FName& BoneName : AllBoneNames)
	{
		FBodyInstance* BodyInst = Mesh->GetBodyInstance(BoneName);
		if (BodyInst)
		{
			BodyInst->LinearDamping = BodyLinearDamping;
			BodyInst->AngularDamping = BodyAngularDamping;
			BodyInst->UpdateDampingProperties();
		}
	}

	UE_LOG(LogTemp, Log, TEXT("PatientPhysics: Applied damping (Linear=%.1f, Angular=%.1f)"),
		BodyLinearDamping, BodyAngularDamping);
}

void UPatientPhysicsComponent::ApplyRestPose()
{
	if (!Mesh) return;

	// Always tick the pose so physical animation has a leader pose to drive toward.
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);

	if (RestPoseAnimation)
	{
		// Use the assigned frozen rest-pose animation as the physical-animation target.
		Mesh->SetAnimation(RestPoseAnimation);
		Mesh->Play(true);
		Mesh->SetPosition(0.0f);
		Mesh->SetPlayRate(0.0f);
		UE_LOG(LogTemp, Log, TEXT("PatientPhysics: Applied rest pose '%s'"), *RestPoseAnimation->GetName());
	}
	else
	{
		// No animation assigned — clear any animation so the mesh holds its
		// neutral reference (bind) pose. Physical-animation motors will drive
		// toward the ref pose instead of a keyframed animation.
		Mesh->SetAnimation(nullptr);
		UE_LOG(LogTemp, Log, TEXT("PatientPhysics: No rest pose animation — using reference pose."));
	}
}

// ============================================================
// Helpers
// ============================================================

FName UPatientPhysicsComponent::ResolveBoneName(EPatientBoneRole BoneRole) const
{
	if (BoneMapping)
	{
		return BoneMapping->GetBoneName(BoneRole);
	}
	return NAME_None;
}

const FPhysicalAnimProfileData* UPatientPhysicsComponent::FindProfileData(EPhysicalAnimProfile Profile) const
{
	if (!SpineConfig) return nullptr;

	for (const FPhysicalAnimProfileData& Data : SpineConfig->PhysicalAnimProfiles)
	{
		if (Data.Profile == Profile)
		{
			return &Data;
		}
	}
	return nullptr;
}
