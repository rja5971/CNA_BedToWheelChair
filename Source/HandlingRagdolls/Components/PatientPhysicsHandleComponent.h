#pragma once

#include "CoreMinimal.h"
#include "PhysicsEngine/PhysicsHandleComponent.h"
#include "PatientPhysicsHandleComponent.generated.h"

/** A force-limited hand spring. Zero retains the standard handle for non-patient objects. */
UCLASS()
class HANDLINGRAGDOLLS_API UPatientPhysicsHandleComponent : public UPhysicsHandleComponent
{

	GENERATED_BODY()
public:
	void SetPatientForceLimit(float Force) { MaximumForce = Force; UpdateDriveSettings(); }
	float GetPatientForceLimit() const { return MaximumForce; }
protected:
	virtual void UpdateDriveSettings() override;
private:
	float MaximumForce = 0.0f;
};
