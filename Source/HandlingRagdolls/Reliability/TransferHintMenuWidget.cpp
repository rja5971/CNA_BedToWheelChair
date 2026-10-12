#include "TransferHintMenuWidget.h"
#include "../Components/TransferGuidanceComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "../Transfer/TransferManagerActor.h"

void UTransferHintMenuWidget::NativeConstruct()
{
    Super::NativeConstruct();
    if (!WidgetTree) return;
    if (!HintsButton)
    {
        UButton* Existing = Cast<UButton>(WidgetTree->FindWidget(TEXT("ExitButton")));
        UVerticalBox* Rows = Existing ? Cast<UVerticalBox>(Existing->GetParent()) : nullptr;
        if (!Rows) { UE_LOG(LogTemp, Error, TEXT("TransferHintMenu: Existing menu rows not found.")); return; }
        HintsButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("HintsButton"));
        HintsButton->SetStyle(Existing->GetStyle());
        HintsButton->SetClickMethod(Existing->GetClickMethod());
        HintsLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("HintsLabel"));
        if (UTextBlock* ExistingLabel = Cast<UTextBlock>(Existing->GetContent()))
        {
            HintsLabel->SetFont(ExistingLabel->GetFont());
            HintsLabel->SetColorAndOpacity(ExistingLabel->GetColorAndOpacity());
        }
        HintsLabel->SetJustification(ETextJustify::Center);
        HintsLabel->SetVisibility(ESlateVisibility::HitTestInvisible);
        HintsButton->AddChild(HintsLabel);
        UVerticalBoxSlot* HintSlot = Rows->AddChildToVerticalBox(HintsButton);
        HintSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        HintSlot->SetPadding(FMargin(0, 4));
        HintsButton->OnClicked.AddDynamic(this, &UTransferHintMenuWidget::ToggleHints);
    }
    RefreshHintLabel();
}
void UTransferHintMenuWidget::RefreshHintLabel()
{
    UGameInstance* Instance = GetGameInstance();
    const UTransferHintsSubsystem* Settings = Instance ? Instance->GetSubsystem<UTransferHintsSubsystem>() : nullptr;
    if (HintsLabel) HintsLabel->SetText(FText::FromString(!Settings || Settings->AreHintsEnabled() ? TEXT("Hints: On") : TEXT("Hints: Off")));
}
void UTransferHintMenuWidget::ToggleHints()
{
    if (UGameInstance* Instance = GetGameInstance())
    {
        UTransferHintsSubsystem* Settings = Instance->GetSubsystem<UTransferHintsSubsystem>();
        Settings->SetHintsEnabled(!Settings->AreHintsEnabled());
        for (TActorIterator<ATransferManagerActor> It(GetWorld()); It; ++It)
            if (It->GetTransferGuidance()) It->GetTransferGuidance()->RefreshHints();
    }
    RefreshHintLabel();
}
