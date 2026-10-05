#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "PatientBedAnimInstance.generated.h"

/** Game-thread pose input, copied into the proxy before parallel animation evaluation. */
UCLASS(Transient)
class HANDLINGRAGDOLLS_API UPatientBedAnimInstance : public UAnimInstance
{
	GENERATED_BODY()
public:
	TArray<FTransform> TargetLocalPose;
	void SetTargetPose(const TArray<FTransform>& Pose);
protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) override;
};
