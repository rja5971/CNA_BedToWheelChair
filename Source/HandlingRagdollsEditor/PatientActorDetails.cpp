#include "PatientActorDetails.h"
#include "BoneAutoDetector.h"
#include "StateConfigDebugExporter.h"
#include "Patient/PatientActor.h"
#include "Patient/PatientBoneMapping.h"

#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"

#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"

#include "Engine/SkeletalMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"

#define LOCTEXT_NAMESPACE "PatientActorDetails"

TSharedRef<IDetailCustomization> FPatientActorDetails::MakeInstance()
{
	return MakeShareable(new FPatientActorDetails);
}

void FPatientActorDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	CachedDetailBuilder = &DetailBuilder;

	// Get the objects being edited
	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
	DetailBuilder.GetObjectsBeingCustomized(ObjectsBeingCustomized);

	if (ObjectsBeingCustomized.Num() == 0)
	{
		return;
	}

	CachedPatientActor = Cast<APatientActor>(ObjectsBeingCustomized[0].Get());
	if (!CachedPatientActor.IsValid())
	{
		return;
	}

	// Refresh bone name list from the assigned mesh
	RefreshBoneNames();
	RefreshSelectionsFromMapping();

	// --- Patient Setup Category (at the top) ---
	IDetailCategoryBuilder& SetupCategory = DetailBuilder.EditCategory(
		TEXT("Patient Setup"),
		LOCTEXT("PatientSetupCategory", "Patient Setup"),
		ECategoryPriority::Important
	);

	// Auto Setup Button
	SetupCategory.AddCustomRow(LOCTEXT("AutoSetupRow", "Auto Setup"))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("AutoSetupLabel", "Quick Setup"))
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	.MaxDesiredWidth(200.0f)
	[
		SNew(SButton)
		.Text(LOCTEXT("AutoSetupButton", "Auto Setup Patient"))
		.HAlign(HAlign_Center)
		.OnClicked(FOnClicked::CreateRaw(this, &FPatientActorDetails::OnAutoSetupClicked))
	];

	// Export State Configs Button
	SetupCategory.AddCustomRow(LOCTEXT("ExportConfigsRow", "Export Configs"))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("ExportConfigsLabel", "Debug"))
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	.MaxDesiredWidth(200.0f)
	[
		SNew(SButton)
		.Text(LOCTEXT("ExportConfigsButton", "Export All State Configs"))
		.HAlign(HAlign_Center)
		.OnClicked_Lambda([]() -> FReply
		{
			UStateConfigDebugExporter::ExportAllStateConfigs();
			return FReply::Handled();
		})
	];

	// --- Bone Mapping Wizard Section ---
	IDetailCategoryBuilder& WizardCategory = DetailBuilder.EditCategory(
		TEXT("Bone Mapping Wizard"),
		LOCTEXT("BoneMappingWizardCategory", "Bone Mapping Wizard"),
		ECategoryPriority::Important
	);

	// Re-detect All button
	WizardCategory.AddCustomRow(LOCTEXT("RedetectRow", "Re-detect"))
	.WholeRowContent()
	[
		SNew(SButton)
		.Text(LOCTEXT("RedetectButton", "Re-detect All Bones"))
		.HAlign(HAlign_Center)
		.OnClicked(FOnClicked::CreateRaw(this, &FPatientActorDetails::OnRedetectAllClicked))
	];

	// For each bone role, add a row with dropdown and status icon
	const UEnum* BoneRoleEnum = StaticEnum<EPatientBoneRole>();
	if (!BoneRoleEnum)
	{
		return;
	}

	const int32 NumRoles = BoneRoleEnum->NumEnums() - 1; // Exclude _MAX
	for (int32 i = 0; i < NumRoles; ++i)
	{
		EPatientBoneRole Role = static_cast<EPatientBoneRole>(BoneRoleEnum->GetValueByIndex(i));
		FText RoleDisplayName = BoneRoleEnum->GetDisplayNameTextByIndex(i);

		// Ensure we have a selection entry for this role
		if (!CurrentBoneSelections.Contains(Role))
		{
			CurrentBoneSelections.Add(Role, MakeShareable(new FName(NAME_None)));
		}

		WizardCategory.AddCustomRow(RoleDisplayName)
		.NameContent()
		[
			SNew(STextBlock)
			.Text(RoleDisplayName)
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		.MaxDesiredWidth(300.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SComboBox<TSharedPtr<FName>>)
				.OptionsSource(&BoneNameOptions)
				.OnSelectionChanged_Lambda([this, Role](TSharedPtr<FName> NewValue, ESelectInfo::Type SelectInfo)
				{
					OnBoneSelectionChanged(NewValue, SelectInfo, Role);
				})
				.OnGenerateWidget(SComboBox<TSharedPtr<FName>>::FOnGenerateWidget::CreateRaw(this, &FPatientActorDetails::GenerateBoneComboWidget))
				.InitiallySelectedItem(CurrentBoneSelections.Contains(Role) ? CurrentBoneSelections[Role] : nullptr)
				[
					SNew(STextBlock)
					.Text_Raw(this, &FPatientActorDetails::GetCurrentBoneText, Role)
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SImage)
				.Image_Lambda([this, Role]() -> const FSlateBrush*
				{
					bool bIsMapped = CurrentBoneSelections.Contains(Role) && 
						CurrentBoneSelections[Role].IsValid() && 
						*CurrentBoneSelections[Role] != NAME_None;
					return bIsMapped
						? FAppStyle::GetBrush("Icons.Check")
						: FAppStyle::GetBrush("Icons.X");
				})
				.ColorAndOpacity_Lambda([this, Role]() -> FSlateColor
				{
					bool bIsMapped = CurrentBoneSelections.Contains(Role) &&
						CurrentBoneSelections[Role].IsValid() &&
						*CurrentBoneSelections[Role] != NAME_None;
					return bIsMapped ? FSlateColor(FLinearColor::Green) : FSlateColor(FLinearColor::Red);
				})
			]
		];
	}
}

FReply FPatientActorDetails::OnAutoSetupClicked()
{
	if (!CachedPatientActor.IsValid())
	{
		return FReply::Handled();
	}

	// Get the skeletal mesh
	USkeletalMesh* Mesh = GetPatientSkeletalMesh();
	if (!Mesh)
	{
		FNotificationInfo Info(LOCTEXT("NoMeshError", "Error: No Skeletal Mesh assigned to PatientActor!"));
		Info.ExpireDuration = 4.0f;
		Info.bUseLargeFont = false;
		FSlateNotificationManager::Get().AddNotification(Info);
		return FReply::Handled();
	}

	// Auto-detect bones
	TMap<EPatientBoneRole, FName> DetectedBones = FBoneAutoDetector::DetectBones(Mesh);

	// Create the bone mapping data asset
	FString MeshName = Mesh->GetName();
	FString PackagePath = TEXT("/Game/PatientSetup");
	FString AssetName = FString::Printf(TEXT("DA_BoneMapping_%s"), *MeshName);
	FString FullPath = PackagePath / AssetName;

	// Create or find the package
	UPackage* Package = CreatePackage(*FullPath);
	if (!Package)
	{
		FNotificationInfo Info(LOCTEXT("PackageError", "Error: Could not create package for bone mapping asset!"));
		Info.ExpireDuration = 4.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
		return FReply::Handled();
	}

	Package->FullyLoad();

	// Create the data asset
	UPatientBoneMapping* NewMapping = NewObject<UPatientBoneMapping>(Package, *AssetName, RF_Public | RF_Standalone);
	if (!NewMapping)
	{
		FNotificationInfo Info(LOCTEXT("AssetCreateError", "Error: Could not create bone mapping data asset!"));
		Info.ExpireDuration = 4.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
		return FReply::Handled();
	}

	// Fill the mappings
	NewMapping->Mappings.Empty();
	for (const auto& Pair : DetectedBones)
	{
		FBoneRoleEntry Entry;
		Entry.Role = Pair.Key;
		Entry.BoneName = Pair.Value;
		NewMapping->Mappings.Add(Entry);
	}

	// Mark dirty and save
	NewMapping->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(NewMapping);

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	FString PackageFilename = FPackageName::LongPackageNameToFilename(FullPath, FPackageName::GetAssetPackageExtension());
	UPackage::SavePackage(Package, NewMapping, *PackageFilename, SaveArgs);

	// Assign to the patient actor
	CachedPatientActor->Modify();
	// Use a property handle approach — find and set the BoneMapping property
	UClass* PatientClass = CachedPatientActor->GetClass();
	FProperty* BoneMappingProp = PatientClass->FindPropertyByName(TEXT("BoneMapping"));
	if (BoneMappingProp)
	{
		FObjectProperty* ObjProp = CastField<FObjectProperty>(BoneMappingProp);
		if (ObjProp)
		{
			ObjProp->SetObjectPropertyValue(ObjProp->ContainerPtrToValuePtr<void>(CachedPatientActor.Get()), NewMapping);
		}
	}

	// Show success notification
	FText SuccessMsg = FText::Format(
		LOCTEXT("AutoSetupSuccess", "Patient auto-setup complete! Mapped {0}/{1} bone roles."),
		FText::AsNumber(DetectedBones.Num()),
		FText::AsNumber(static_cast<int32>(StaticEnum<EPatientBoneRole>()->NumEnums() - 1))
	);
	FNotificationInfo Info(SuccessMsg);
	Info.ExpireDuration = 5.0f;
	Info.bUseLargeFont = false;
	FSlateNotificationManager::Get().AddNotification(Info);

	// Refresh the details panel
	if (CachedDetailBuilder)
	{
		CachedDetailBuilder->ForceRefreshDetails();
	}

	return FReply::Handled();
}

FReply FPatientActorDetails::OnRedetectAllClicked()
{
	if (!CachedPatientActor.IsValid())
	{
		return FReply::Handled();
	}

	USkeletalMesh* Mesh = GetPatientSkeletalMesh();
	if (!Mesh)
	{
		FNotificationInfo Info(LOCTEXT("NoMeshRedetect", "Error: No Skeletal Mesh assigned!"));
		Info.ExpireDuration = 3.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
		return FReply::Handled();
	}

	TMap<EPatientBoneRole, FName> DetectedBones = FBoneAutoDetector::DetectBones(Mesh);

	// Update selections and the data asset
	for (const auto& Pair : DetectedBones)
	{
		if (CurrentBoneSelections.Contains(Pair.Key))
		{
			*CurrentBoneSelections[Pair.Key] = Pair.Value;
		}
		else
		{
			CurrentBoneSelections.Add(Pair.Key, MakeShareable(new FName(Pair.Value)));
		}
		UpdateBoneMappingEntry(Pair.Key, Pair.Value);
	}

	FNotificationInfo Info(FText::Format(
		LOCTEXT("RedetectSuccess", "Re-detected {0} bone roles."),
		FText::AsNumber(DetectedBones.Num())
	));
	Info.ExpireDuration = 3.0f;
	FSlateNotificationManager::Get().AddNotification(Info);

	if (CachedDetailBuilder)
	{
		CachedDetailBuilder->ForceRefreshDetails();
	}

	return FReply::Handled();
}

void FPatientActorDetails::OnBoneSelectionChanged(TSharedPtr<FName> NewValue, ESelectInfo::Type SelectInfo, EPatientBoneRole Role)
{
	if (!NewValue.IsValid())
	{
		return;
	}

	if (CurrentBoneSelections.Contains(Role))
	{
		*CurrentBoneSelections[Role] = *NewValue;
	}
	else
	{
		CurrentBoneSelections.Add(Role, NewValue);
	}

	UpdateBoneMappingEntry(Role, *NewValue);
}

TSharedRef<SWidget> FPatientActorDetails::GenerateBoneComboWidget(TSharedPtr<FName> InItem)
{
	FText DisplayText = InItem.IsValid() ? FText::FromName(*InItem) : LOCTEXT("None", "None");
	return SNew(STextBlock)
		.Text(DisplayText)
		.Font(IDetailLayoutBuilder::GetDetailFont());
}

FText FPatientActorDetails::GetCurrentBoneText(EPatientBoneRole Role) const
{
	if (CurrentBoneSelections.Contains(Role) && CurrentBoneSelections[Role].IsValid())
	{
		FName BoneName = *CurrentBoneSelections[Role];
		if (BoneName != NAME_None)
		{
			return FText::FromName(BoneName);
		}
	}
	return LOCTEXT("NoneSelected", "None");
}

void FPatientActorDetails::RefreshBoneNames()
{
	BoneNameOptions.Empty();

	// Add a "None" option
	BoneNameOptions.Add(MakeShareable(new FName(NAME_None)));

	USkeletalMesh* Mesh = GetPatientSkeletalMesh();
	if (!Mesh)
	{
		return;
	}

	const FReferenceSkeleton& RefSkeleton = Mesh->GetRefSkeleton();
	const int32 NumBones = RefSkeleton.GetNum();

	for (int32 i = 0; i < NumBones; ++i)
	{
		BoneNameOptions.Add(MakeShareable(new FName(RefSkeleton.GetBoneName(i))));
	}
}

void FPatientActorDetails::RefreshSelectionsFromMapping()
{
	CurrentBoneSelections.Empty();

	if (!CachedPatientActor.IsValid())
	{
		return;
	}

	// Get the BoneMapping property
	UClass* PatientClass = CachedPatientActor->GetClass();
	FProperty* BoneMappingProp = PatientClass->FindPropertyByName(TEXT("BoneMapping"));
	if (!BoneMappingProp)
	{
		return;
	}

	FObjectProperty* ObjProp = CastField<FObjectProperty>(BoneMappingProp);
	if (!ObjProp)
	{
		return;
	}

	UPatientBoneMapping* Mapping = Cast<UPatientBoneMapping>(
		ObjProp->GetObjectPropertyValue(ObjProp->ContainerPtrToValuePtr<void>(CachedPatientActor.Get()))
	);

	if (!Mapping)
	{
		return;
	}

	// Populate selections from the mapping
	for (const FBoneRoleEntry& Entry : Mapping->Mappings)
	{
		// Find the matching shared pointer in BoneNameOptions
		TSharedPtr<FName> FoundOption = nullptr;
		for (const auto& Option : BoneNameOptions)
		{
			if (Option.IsValid() && *Option == Entry.BoneName)
			{
				FoundOption = Option;
				break;
			}
		}

		if (!FoundOption.IsValid())
		{
			FoundOption = MakeShareable(new FName(Entry.BoneName));
		}

		CurrentBoneSelections.Add(Entry.Role, FoundOption);
	}
}

USkeletalMesh* FPatientActorDetails::GetPatientSkeletalMesh() const
{
	if (!CachedPatientActor.IsValid())
	{
		return nullptr;
	}

	USkeletalMeshComponent* MeshComp = CachedPatientActor->FindComponentByClass<USkeletalMeshComponent>();
	if (!MeshComp)
	{
		return nullptr;
	}

	return MeshComp->GetSkeletalMeshAsset();
}

void FPatientActorDetails::UpdateBoneMappingEntry(EPatientBoneRole Role, FName BoneName)
{
	if (!CachedPatientActor.IsValid())
	{
		return;
	}

	// Get the BoneMapping data asset
	UClass* PatientClass = CachedPatientActor->GetClass();
	FProperty* BoneMappingProp = PatientClass->FindPropertyByName(TEXT("BoneMapping"));
	if (!BoneMappingProp)
	{
		return;
	}

	FObjectProperty* ObjProp = CastField<FObjectProperty>(BoneMappingProp);
	if (!ObjProp)
	{
		return;
	}

	UPatientBoneMapping* Mapping = Cast<UPatientBoneMapping>(
		ObjProp->GetObjectPropertyValue(ObjProp->ContainerPtrToValuePtr<void>(CachedPatientActor.Get()))
	);

	if (!Mapping)
	{
		return;
	}

	// Update or add the entry
	Mapping->Modify();
	bool bFound = false;
	for (FBoneRoleEntry& Entry : Mapping->Mappings)
	{
		if (Entry.Role == Role)
		{
			Entry.BoneName = BoneName;
			bFound = true;
			break;
		}
	}

	if (!bFound)
	{
		FBoneRoleEntry NewEntry;
		NewEntry.Role = Role;
		NewEntry.BoneName = BoneName;
		Mapping->Mappings.Add(NewEntry);
	}

	Mapping->MarkPackageDirty();
}

#undef LOCTEXT_NAMESPACE
