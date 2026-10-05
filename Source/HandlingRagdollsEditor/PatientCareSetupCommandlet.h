#pragma once
#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "PatientCareSetupCommandlet.generated.h"

/** Inspect or migrate only the patient bed/grab assets; preserves media/menu patches. */
UCLASS()
class UPatientCareSetupCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UPatientCareSetupCommandlet();
	virtual int32 Main(const FString& Params) override;
};
