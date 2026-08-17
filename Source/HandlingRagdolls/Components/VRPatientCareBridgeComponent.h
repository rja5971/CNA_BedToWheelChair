#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VRPatientCareBridgeComponent.generated.h"

class UGrabComponent;
class UMotionControllerComponent;
class UEnhancedInputComponent;

/** Adapts the existing CNA VR pawn to the native patient-care grab system. */
UCLASS(ClassGroup = (PatientCare))
class HANDLINGRAGDOLLS_API UVRPatientCareBridgeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVRPatientCareBridgeComponent();

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UGrabComponent> LeftGrabber;

	UPROPERTY(Transient)
	TObjectPtr<UGrabComponent> RightGrabber;

	UPROPERTY(Transient)
	TObjectPtr<UMotionControllerComponent> LeftController;

	UPROPERTY(Transient)
	TObjectPtr<UMotionControllerComponent> RightController;

	bool bInputBound = false;
	bool bReadyLogged = false;
	bool TryInstall();
	UMotionControllerComponent* FindController(bool bLeft) const;
	UGrabComponent* CreateGrabber(FName Name, UMotionControllerComponent* Controller);
	void BindInput(UEnhancedInputComponent* EnhancedInput);
	void GrabLeft();
	void ReleaseLeft();
	void GrabRight();
	void ReleaseRight();
};
