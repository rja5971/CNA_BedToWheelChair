#pragma once

#include "Blueprint/UserWidget.h"
#include "TrainingMediaErrorWidget.generated.h"

class UTrainingMediaControllerComponent;

UCLASS()
class HANDLINGRAGDOLLS_API UTrainingMediaErrorWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	void SetController(UTrainingMediaControllerComponent* Value);
protected:
	virtual void NativeOnInitialized() override;
private:
	TWeakObjectPtr<UTrainingMediaControllerComponent> Controller;
	UFUNCTION() void RetryClicked();
};
