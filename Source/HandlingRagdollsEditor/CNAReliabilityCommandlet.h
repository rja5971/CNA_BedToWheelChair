#pragma once

#include "Commandlets/Commandlet.h"
#include "CNAReliabilityCommandlet.generated.h"

/** Inspect and migrate the project's binary Blueprint assets using Unreal's own graph APIs. */
UCLASS()
class UCNAReliabilityCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UCNAReliabilityCommandlet();
	virtual int32 Main(const FString& Params) override;
};
