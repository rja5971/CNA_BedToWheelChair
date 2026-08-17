#pragma once

#include "CoreMinimal.h"
#include "Patient/PatientBoneMapping.h"

class USkeletalMesh;

/**
 * Static utility class that auto-detects bone roles from a skeletal mesh
 * by scanning bone names with pattern matching.
 */
class FBoneAutoDetector
{
public:
	/**
	 * Scan all bone names in the given mesh and match them to logical patient bone roles.
	 * Pattern matching is case-insensitive.
	 *
	 * @param Mesh The skeletal mesh to analyze
	 * @return Map of detected bone roles to their matching bone names
	 */
	static TMap<EPatientBoneRole, FName> DetectBones(const USkeletalMesh* Mesh);

private:
	/** Check if a bone name contains a left-side indicator (_l, left, .l) */
	static bool IsLeftBone(const FString& BoneName);

	/** Check if a bone name contains a right-side indicator (_r, right, .r) */
	static bool IsRightBone(const FString& BoneName);

	/** Get all bone names from the mesh as an array of FName */
	static TArray<FName> GetAllBoneNames(const USkeletalMesh* Mesh);

	/** Get the parent bone index for a given bone index */
	static int32 GetBoneDepth(const USkeletalMesh* Mesh, int32 BoneIndex);
};
