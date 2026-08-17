#include "StateConfigDebugExporter.h"
#include "Patient/PatientStateConfig.h"
#include "Patient/PatientTypes.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/DataAsset.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"

static FString BehaviorToString(EBoneBehavior B)
{
	switch (B)
	{
	case EBoneBehavior::Anchored: return TEXT("Anchored");
	case EBoneBehavior::Stiff:    return TEXT("Stiff");
	case EBoneBehavior::Free:     return TEXT("Free");
	case EBoneBehavior::Pivot:    return TEXT("Pivot");
	default:                      return TEXT("Unknown");
	}
}

static FString BoneGroupToString(EPatientBoneGroup G)
{
	switch (G)
	{
	case EPatientBoneGroup::WholeBodyBelowPelvis: return TEXT("WholeBodyBelowPelvis");
	case EPatientBoneGroup::Pelvis:               return TEXT("Pelvis");
	case EPatientBoneGroup::Spine:                return TEXT("Spine");
	case EPatientBoneGroup::Neck:                 return TEXT("Neck");
	case EPatientBoneGroup::Head:                 return TEXT("Head");
	case EPatientBoneGroup::LeftArm:              return TEXT("LeftArm");
	case EPatientBoneGroup::RightArm:             return TEXT("RightArm");
	case EPatientBoneGroup::LeftLeg:              return TEXT("LeftLeg");
	case EPatientBoneGroup::RightLeg:             return TEXT("RightLeg");
	default:                                      return TEXT("Unknown");
	}
}

static FString PatientStateToString(EPatientState S)
{
	switch (S)
	{
	case EPatientState::LyingDown:        return TEXT("LyingDown");
	case EPatientState::BeingSupported:   return TEXT("BeingSupported");
	case EPatientState::BeltAttached:     return TEXT("BeltAttached");
	case EPatientState::BeingLifted:      return TEXT("BeingLifted");
	case EPatientState::BeingTransferred: return TEXT("BeingTransferred");
	case EPatientState::Seated:           return TEXT("Seated");
	case EPatientState::Injured:          return TEXT("Injured");
	default:                              return TEXT("Unknown");
	}
}

FString UStateConfigDebugExporter::FormatStateConfig(UPatientStateConfig* Config)
{
	if (!Config) return TEXT("(null config)");

	FString Result;
	Result += FString::Printf(TEXT("=== %s ===\n"), *Config->GetName());
	Result += FString::Printf(TEXT("  State: %s\n"), *PatientStateToString(Config->State));
	Result += FString::Printf(TEXT("  DefaultBehavior: %s\n"), *BehaviorToString(Config->DefaultBehavior));
	Result += FString::Printf(TEXT("  Stiff: Orientation=%.0f, AngVel=%.0f, MaxForce=%.0f\n"),
		Config->StiffOrientationStrength, Config->StiffAngularVelocityStrength, Config->StiffMaxAngularForce);
	Result += FString::Printf(TEXT("  Free:  Orientation=%.0f, AngVel=%.0f\n"),
		Config->FreeOrientationStrength, Config->FreeAngularVelocityStrength);
	Result += FString::Printf(TEXT("  BoneGroups (%d entries):\n"), Config->BoneGroups.Num());

	for (int32 i = 0; i < Config->BoneGroups.Num(); i++)
	{
		const FBoneGroupBehavior& BG = Config->BoneGroups[i];
		FString StrOverride = (BG.OrientationStrengthOverride >= 0.0f)
			? FString::Printf(TEXT(" (strength=%.0f)"), BG.OrientationStrengthOverride)
			: TEXT("");
		Result += FString::Printf(TEXT("    [%d] %s = %s%s\n"),
			i, *BoneGroupToString(BG.Group), *BehaviorToString(BG.Behavior), *StrOverride);
	}

	Result += TEXT("\n");
	return Result;
}

void UStateConfigDebugExporter::ExportAllStateConfigs()
{
	// Find all UPatientStateConfig assets via Asset Registry
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetData> AssetDataList;
	AssetRegistry.GetAssetsByClass(UPatientStateConfig::StaticClass()->GetClassPathName(), AssetDataList);

	FString FullDump;
	FullDump += TEXT("============================================\n");
	FullDump += TEXT("  PATIENT STATE CONFIG DUMP\n");
	FullDump += FString::Printf(TEXT("  Generated: %s\n"), *FDateTime::Now().ToString());
	FullDump += FString::Printf(TEXT("  Found %d state config assets\n"), AssetDataList.Num());
	FullDump += TEXT("============================================\n\n");

	if (AssetDataList.Num() == 0)
	{
		FullDump += TEXT("  No UPatientStateConfig data assets found in project!\n");
		FullDump += TEXT("  Make sure you created them via: Content Browser > Misc > Data Asset > PatientStateConfig\n");
	}

	for (const FAssetData& AssetData : AssetDataList)
	{
		UPatientStateConfig* Config = Cast<UPatientStateConfig>(AssetData.GetAsset());
		if (Config)
		{
			FullDump += FormatStateConfig(Config);
		}
		else
		{
			FullDump += FString::Printf(TEXT("  (Failed to load: %s)\n\n"), *AssetData.AssetName.ToString());
		}
	}

	FullDump += TEXT("============================================\n");
	FullDump += TEXT("  END OF DUMP\n");
	FullDump += TEXT("============================================\n");

	// Print to Output Log
	UE_LOG(LogTemp, Warning, TEXT("\n%s"), *FullDump);

	// Also write to file
	FString FilePath = FPaths::ProjectSavedDir() / TEXT("StateConfigDump.txt");
	if (FFileHelper::SaveStringToFile(FullDump, *FilePath))
	{
		UE_LOG(LogTemp, Warning, TEXT("State config dump written to: %s"), *FilePath);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to write state config dump to file!"));
	}
}
