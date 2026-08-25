#include "PatientSeatedAnimInstance.h"

#include "../Components/SeatedTransitionComponent.h"
#include "../Patient/PatientActor.h"
#include "../Transfer/WheelchairActor.h"
#include "Components/SkeletalMeshComponent.h"

void UPatientSeatedAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	Patient = Cast<APatientActor>(GetOwningActor());
}

void UPatientSeatedAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (!Patient)
	{
		Patient = Cast<APatientActor>(GetOwningActor());
	}

	USkeletalMeshComponent* Mesh = GetSkelMeshComponent();
	USeatedTransitionComponent* Transition = Patient ? Patient->GetSeatedTransitionComponent() : nullptr;
	AWheelchairActor* Chair = Transition ? Transition->GetTargetWheelchair() : nullptr;
	const bool bHasValidTargets = Mesh && Chair && Transition->IsSeatedLocked();
	const float DesiredAlpha = bHasValidTargets ? 1.0f : 0.0f;
	FootIKAlpha = FMath::FInterpTo(FootIKAlpha, DesiredAlpha, DeltaSeconds, IKBlendSpeed);

	if (!Mesh || !Chair)
	{
		return;
	}

	const FTransform MeshWorld = Mesh->GetComponentTransform();
	LeftFootTargetCS = MeshWorld.InverseTransformPosition(Chair->GetLeftFootTargetTransform().GetLocation());
	RightFootTargetCS = MeshWorld.InverseTransformPosition(Chair->GetRightFootTargetTransform().GetLocation());
	LeftKneeTargetCS = MeshWorld.InverseTransformPosition(Chair->GetLeftKneeTargetTransform().GetLocation());
	RightKneeTargetCS = MeshWorld.InverseTransformPosition(Chair->GetRightKneeTargetTransform().GetLocation());
}
