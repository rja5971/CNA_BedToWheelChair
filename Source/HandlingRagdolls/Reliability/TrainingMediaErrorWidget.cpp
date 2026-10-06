#include "TrainingMediaErrorWidget.h"
#include "TrainingMediaControllerComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

void UTrainingMediaErrorWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	UBorder* Border = WidgetTree->ConstructWidget<UBorder>();
	Border->SetBrushColor(FLinearColor(0.025f, 0.025f, 0.025f, 1));
	Border->SetPadding(FMargin(24));
	UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>();
	Border->SetContent(Box);
	UTextBlock* Message = WidgetTree->ConstructWidget<UTextBlock>();
	Message->SetText(NSLOCTEXT("CNAMedia", "PlaybackError", "Video could not play.\nSelect Retry, or use Restart in the menu."));
	Message->SetJustification(ETextJustify::Center);
	Message->SetAutoWrapText(true);
	FSlateFontInfo Font = Message->GetFont(); Font.Size = 30; Message->SetFont(Font);
	Box->AddChildToVerticalBox(Message)->SetPadding(FMargin(0, 0, 0, 20));
	UButton* Retry = WidgetTree->ConstructWidget<UButton>();
	Retry->SetClickMethod(EButtonClickMethod::DownAndUp);
	UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>();
	Label->SetText(NSLOCTEXT("CNAMedia", "Retry", "Retry"));
	Label->SetFont(Font);
	Label->SetJustification(ETextJustify::Center);
	Retry->AddChild(Label);
	Retry->OnClicked.AddDynamic(this, &UTrainingMediaErrorWidget::RetryClicked);
	Box->AddChildToVerticalBox(Retry);
	WidgetTree->RootWidget = Border;
}

void UTrainingMediaErrorWidget::RetryClicked()
{
	if (Controller.IsValid()) Controller->RetryPlayback();
}

void UTrainingMediaErrorWidget::SetController(UTrainingMediaControllerComponent* Value)
{
	Controller = Value;
}
