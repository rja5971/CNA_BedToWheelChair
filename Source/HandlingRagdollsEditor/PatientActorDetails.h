#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "Patient/PatientBoneMapping.h"

class APatientActor;
class USkeletalMesh;

/**
 * Detail customization for APatientActor in the editor.
 * Adds:
 * - 'Auto Setup Patient' button
 * - Bone Mapping Wizard section with per-role dropdowns
 */
class FPatientActorDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	/** The patient actor being edited */
	TWeakObjectPtr<APatientActor> CachedPatientActor;

	/** Cached bone names from the skeletal mesh */
	TArray<TSharedPtr<FName>> BoneNameOptions;

	/** Currently selected bone for each role (displayed in the wizard) */
	TMap<EPatientBoneRole, TSharedPtr<FName>> CurrentBoneSelections;

	/** Run auto-setup: detect bones, create data asset, assign it */
	FReply OnAutoSetupClicked();

	/** Re-detect all bones and update the wizard dropdowns */
	FReply OnRedetectAllClicked();

	/** Called when a bone dropdown selection changes */
	void OnBoneSelectionChanged(TSharedPtr<FName> NewValue, ESelectInfo::Type SelectInfo, EPatientBoneRole Role);

	/** Generate the display text for a bone dropdown item */
	TSharedRef<SWidget> GenerateBoneComboWidget(TSharedPtr<FName> InItem);

	/** Get the text for the currently selected bone for a given role */
	FText GetCurrentBoneText(EPatientBoneRole Role) const;

	/** Refresh the cached bone name list from the patient's mesh */
	void RefreshBoneNames();

	/** Refresh the wizard selections from the current BoneMapping data asset */
	void RefreshSelectionsFromMapping();

	/** Get the skeletal mesh from the patient actor */
	USkeletalMesh* GetPatientSkeletalMesh() const;

	/** Update a single mapping entry in the BoneMapping data asset */
	void UpdateBoneMappingEntry(EPatientBoneRole Role, FName BoneName);

	/** Force refresh of the details panel */
	IDetailLayoutBuilder* CachedDetailBuilder = nullptr;
};
