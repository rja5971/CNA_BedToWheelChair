#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "PatientSeatedAnimInstance.generated.h"

class APatientActor;

/**
 * Game-thread bridge between wheelchair rig markers and the seated AnimGraph.
 * Positions are converted to mesh component space so Two Bone IK nodes can use
 * them directly without depending on a particular wheelchair world rotation.
 */
UCLASS(Blueprintable, Transient)
class HANDLINGRAGDOLLS_API UPatientSeatedAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	UPROPERTY(BlueprintReadOnly, Category = "Wheelchair Foot IK")
	FVector LeftFootTargetCS = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Wheelchair Foot IK")
	FVector RightFootTargetCS = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Wheelchair Foot IK")
	FVector LeftKneeTargetCS = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Wheelchair Foot IK")
	FVector RightKneeTargetCS = FVector::ZeroVector;

	/** Smooth alpha shared by both legs; reaches one shortly after chair lock. */
	UPROPERTY(BlueprintReadOnly, Category = "Wheelchair Foot IK")
	float FootIKAlpha = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wheelchair Foot IK", meta = (ClampMin = "0.0"))
	float IKBlendSpeed = 8.0f;

private:
	UPROPERTY(Transient)
	TObjectPtr<APatientActor> Patient;
};
