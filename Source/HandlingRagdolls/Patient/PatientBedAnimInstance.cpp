#include "PatientBedAnimInstance.h"
#include "Animation/AnimInstanceProxy.h"

struct FPatientBedAnimProxy : public FAnimInstanceProxy
{
	FPatientBedAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}
	TArray<FTransform> Pose;
	virtual void PreUpdate(UAnimInstance* Instance, float DeltaSeconds) override
	{
		FAnimInstanceProxy::PreUpdate(Instance, DeltaSeconds);
		Pose = CastChecked<UPatientBedAnimInstance>(Instance)->TargetLocalPose;
	}
	virtual bool Evaluate(FPoseContext& Output) override
	{
		Output.ResetToRefPose();
		for (FCompactPoseBoneIndex Index : Output.Pose.ForEachBoneIndex())
		{
			const int32 MeshIndex = Output.Pose.GetBoneContainer().MakeMeshPoseIndex(Index).GetInt();
			if (Pose.IsValidIndex(MeshIndex)) Output.Pose[Index] = Pose[MeshIndex];
		}
		return true;
	}
};

FAnimInstanceProxy* UPatientBedAnimInstance::CreateAnimInstanceProxy()
{
	return new FPatientBedAnimProxy(this);
}

void UPatientBedAnimInstance::SetTargetPose(const TArray<FTransform>& Pose)
{
	TargetLocalPose = Pose;
	// Initialize evaluation even before the first animation PreUpdate, and wait
	// for outstanding animation work before changing its thread-owned pose.
	GetProxyOnGameThread<FPatientBedAnimProxy>().Pose = Pose;
}

void UPatientBedAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy)
{
	delete Proxy;
}
