#include "HandlingRagdollsEditorModule.h"
#include "PatientActorDetails.h"
#include "Patient/PatientActor.h"
#include "PropertyEditorModule.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "HandlingRagdollsEditor"

void FHandlingRagdollsEditorModule::StartupModule()
{
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	
	PropertyModule.RegisterCustomClassLayout(
		APatientActor::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FPatientActorDetails::MakeInstance)
	);

	PropertyModule.NotifyCustomizationModuleChanged();
}

void FHandlingRagdollsEditorModule::ShutdownModule()
{
	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomClassLayout(APatientActor::StaticClass()->GetFName());
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FHandlingRagdollsEditorModule, HandlingRagdollsEditor)
