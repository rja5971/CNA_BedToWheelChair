// Fill out your copyright notice in the Description page of Project Settings.

#include "CooperationRampComponent.h"
#include "PatientPhysicsComponent.h"
#include "../Patient/PatientBoneMapping.h"
#include "Curves/CurveFloat.h"
#include "PhysicsEngine/PhysicalAnimationComponent.h"

UCooperationRampComponent::UCooperationRampComponent()
{
	PrimaryComponentTick.bCanEverTick = false; // Driven externally via TickRamp()
}

void UCooperationRampComponent::Initialize(UPatientPhysicsComponent* InPhysicsComponent, UPatientBoneMapping* InBoneMapping)
{
	PhysicsComponent = InPhysicsComponent;
	BoneMappingRef = InBoneMapping;
}

void UCooperationRampComponent::TickRamp(float TorsoAngle, float SitUprightThreshold)
{
	if (!PhysicsComponent || !BoneMappingRef)
	{
		return;
	}

	// Only blend when torso has passed the cooperation start angle
	if (TorsoAngle >= CoopStartAngle)
	{
		return;
	}

	// Compute the cooperation alpha
	float CoopAlpha;
	if (CooperationCurve)
	{
		// Use designer-authored curve: X = torso angle, Y = alpha
		CoopAlpha = CooperationCurve->GetFloatValue(TorsoAngle);
		CoopAlpha = FMath::Clamp(CoopAlpha, 0.0f, MaxCoopAlpha);
	}
	else
	{
		// Default linear mapping: CoopStartAngle → 0, SitUprightThreshold → MaxCoopAlpha
		CoopAlpha = FMath::GetMappedRangeValueClamped(
			FVector2D(CoopStartAngle, SitUprightThreshold),
			FVector2D(0.0f, MaxCoopAlpha),
			TorsoAngle);
	}

	// Resolve pelvis bone
	FName PelvisBone = BoneMappingRef->GetBoneName(EPatientBoneRole::Pelvis);
	if (PelvisBone.IsNone())
	{
		return;
	}

	// Build blended torso settings
	FPhysicalAnimationData BlendedData;
	BlendedData.bIsLocalSimulation = false;
	BlendedData.OrientationStrength = FMath::Lerp(RelaxedOrientationStrength, CooperatingOrientationStrength, CoopAlpha);
	BlendedData.AngularVelocityStrength = FMath::Lerp(RelaxedAngularVelocityStrength, CooperatingAngularVelocityStrength, CoopAlpha);
	BlendedData.PositionStrength = FMath::Lerp(RelaxedPositionStrength, CooperatingPositionStrength, CoopAlpha);
	BlendedData.VelocityStrength = FMath::Lerp(RelaxedVelocityStrength, CooperatingVelocityStrength, CoopAlpha);
	BlendedData.MaxAngularForce = FMath::Lerp(RelaxedMaxAngularForce, CooperatingMaxAngularForce, CoopAlpha);
	BlendedData.MaxLinearForce = FMath::Lerp(RelaxedMaxLinearForce, CooperatingMaxLinearForce, CoopAlpha);

	// Apply to entire body below pelvis
	PhysicsComponent->ApplyCustomSettings(BlendedData, PelvisBone);

	// Build weaker arm settings — arms drape naturally with gravity
	FPhysicalAnimationData ArmData;
	ArmData.bIsLocalSimulation = false;
	ArmData.OrientationStrength = BlendedData.OrientationStrength * ArmStrengthMultiplier;
	ArmData.AngularVelocityStrength = BlendedData.AngularVelocityStrength * ArmDampingMultiplier;
	ArmData.PositionStrength = 0.0f;
	ArmData.VelocityStrength = 0.0f;
	ArmData.MaxAngularForce = BlendedData.MaxAngularForce * ArmStrengthMultiplier;
	ArmData.MaxLinearForce = 0.0f;

	// Apply weaker settings to arm subtrees
	FName LeftClavicle = BoneMappingRef->GetBoneName(EPatientBoneRole::ClavicleLeft);
	FName RightClavicle = BoneMappingRef->GetBoneName(EPatientBoneRole::ClavicleRight);
	if (!LeftClavicle.IsNone())
	{
		PhysicsComponent->ApplyCustomSettingsForBone(ArmData, LeftClavicle);
	}
	if (!RightClavicle.IsNone())
	{
		PhysicsComponent->ApplyCustomSettingsForBone(ArmData, RightClavicle);
	}
}
