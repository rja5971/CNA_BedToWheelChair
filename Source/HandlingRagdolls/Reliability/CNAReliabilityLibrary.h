#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "CNAReliabilityLibrary.generated.h"

/** Small Blueprint adapters retaining the existing binary assets' entry points. */
UCLASS()
class HANDLINGRAGDOLLS_API UCNAReliabilityLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category="CNA|Recovery")
	static void RequestPlayback(AActor* Display);
	UFUNCTION(BlueprintCallable, Category="CNA|Recovery")
	static void CancelPlayback(AActor* Display);
	UFUNCTION(BlueprintCallable, Category="CNA|Recovery", meta=(WorldContext="WorldContextObject"))
	static void RetryPlayback(const UObject* WorldContextObject);
	UFUNCTION(BlueprintCallable, Category="CNA|Recovery", meta=(WorldContext="WorldContextObject"))
	static void RestartTraining(const UObject* WorldContextObject);
	UFUNCTION(BlueprintCallable, Category="CNA|Recovery", meta=(WorldContext="WorldContextObject"))
	static void ResetOrientation(const UObject* WorldContextObject);
	UFUNCTION(BlueprintCallable, Category="CNA|Recovery", meta=(WorldContext="WorldContextObject"))
	static void ExitTraining(const UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category="CNA|UI")
	static void StopMenuInteraction(AActor* Menu);

	static UObject* ObjectProperty(const UObject* Object, FName Name);
	static void CallNoArgs(UObject* Object, FName Function);
};
