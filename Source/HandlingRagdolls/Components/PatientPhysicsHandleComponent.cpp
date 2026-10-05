#include "PatientPhysicsHandleComponent.h"

void UPatientPhysicsHandleComponent::UpdateDriveSettings()
{
	Super::UpdateDriveSettings();
	if (MaximumForce > 0.0f)
	{
		ConstraintInstance.SetLinearPositionDrive(true, true, true);
		ConstraintInstance.SetLinearVelocityDrive(true, true, true);
		ConstraintInstance.SetLinearDriveAccelerationMode(false);
		ConstraintInstance.SetLinearDriveParams(LinearStiffness, LinearDamping, MaximumForce);
	}
}
