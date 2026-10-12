#pragma once
#include "Blueprint/UserWidget.h"
#include "TransferHintMenuWidget.generated.h"

class UButton;
class UTextBlock;

/** Existing menu Blueprint retains its layout/events; this adds one native row. */
UCLASS()
class HANDLINGRAGDOLLS_API UTransferHintMenuWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    virtual void NativeConstruct() override;
    UFUNCTION() void ToggleHints();
    UFUNCTION(BlueprintCallable, Category="CNA|Hints") void RefreshHintLabel();
private:
    UPROPERTY(Transient) TObjectPtr<UButton> HintsButton;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> HintsLabel;
};
