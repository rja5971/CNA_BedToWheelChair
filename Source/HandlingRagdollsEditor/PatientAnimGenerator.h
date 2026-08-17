// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PatientAnimGenerator.generated.h"

class USkeletalMesh;
class UAnimSequence;

/**
 * Editor utility to procedurally generate patient animations (sit-up, etc.)
 * without needing external tools like Blender or Mixamo.
 *
 * Usage: Call from an Editor Utility Blueprint or the console.
 */
UCLASS()
class HANDLINGRAGDOLLSEDITOR_API UPatientAnimGenerator : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Generate a sit-up animation sequence for the given skeletal mesh.
	 * Creates an AnimSequence at the specified path with the patient going
	 * from a folded ~45° position to a fully seated upright position.
	 *
	 * @param Mesh The skeletal mesh to generate the animation for
	 * @param NumFrames Number of frames (at 30fps). Default 30 = 1 second.
	 * @param SavePath Content path to save the asset (e.g., "/Game/Animations/AS_Patient_SitUp")
	 * @return The created AnimSequence, or nullptr on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Patient Animation", meta = (CallInEditor = "true"))
	static UAnimSequence* GenerateSitUpAnimation(USkeletalMesh* Mesh, int32 NumFrames = 30, FString SavePath = TEXT("/Game/Animations/AS_Patient_SitUp"));
};
