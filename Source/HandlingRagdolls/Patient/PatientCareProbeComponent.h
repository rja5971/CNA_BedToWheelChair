#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PatientCareProbeComponent.generated.h"

class APatientActor;
class ABeltActor;
class AWheelchairActor;
class UGrabComponent;

/** Explicit Development-only console probe; never installed in normal play. */
UCLASS()
class HANDLINGRAGDOLLS_API UPatientCareProbeComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UPatientCareProbeComponent();
	void Start();
	virtual void TickComponent(float DeltaTime, ELevelTick Type, FActorComponentTickFunction* Function) override;
private:
	UPROPERTY() TObjectPtr<APatientActor> Patient;
	UPROPERTY() TObjectPtr<ABeltActor> Belt;
	UPROPERTY() TObjectPtr<AWheelchairActor> Chair;
	UPROPERTY() TArray<TObjectPtr<UGrabComponent>> Hands;
	UPROPERTY() TArray<TObjectPtr<USceneComponent>> Origins;
	FTransform InitialTransform;
	FVector HipTarget, ChestTarget, FootTarget;
	FVector Starts[2], Targets[2];
	int32 Stage = 0, Attempt = 0;
	float Elapsed = 0, Duration = 1;
	bool bFinished = false;
	bool Acquire(int32 Hand, FName Bone);
	bool Check(bool Condition, const TCHAR* Description);
	void BeginStage(int32 Next, float Seconds);
	void ResetBed();
	void Finish(bool Passed);
};
