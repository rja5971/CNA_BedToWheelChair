#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "StateConfigDebugExporter.generated.h"

/**
 * Editor utility to dump all PatientStateConfig data assets in the project
 * to the Output Log and optionally to a file. Useful for verifying that
 * bone group behaviors are set up correctly.
 *
 * Usage: Call from editor console: StateConfigDebugExporter.ExportAllStateConfigs()
 * Or use the button in the PatientActor details panel.
 */
UCLASS()
class HANDLINGRAGDOLLSEDITOR_API UStateConfigDebugExporter : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Find and dump all UPatientStateConfig data assets to the Output Log.
	 * Also writes to [ProjectDir]/Saved/StateConfigDump.txt
	 */
	UFUNCTION(BlueprintCallable, Category = "Patient Debug", meta = (CallInEditor = "true"))
	static void ExportAllStateConfigs();

	/**
	 * Dump a single state config asset's values.
	 */
	UFUNCTION(BlueprintCallable, Category = "Patient Debug")
	static FString FormatStateConfig(class UPatientStateConfig* Config);
};
