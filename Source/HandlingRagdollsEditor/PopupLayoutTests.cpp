#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Slate/WidgetRenderer.h"
#include "ImageUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/BufferArchive.h"
#include "HAL/FileManager.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCNAPopupReadabilityTest, "CNA.GuidedTraining.PopupReadability", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCNAPopupReadabilityTest::RunTest(const FString& Parameters)
{
    UGameInstance* Instance = NewObject<UGameInstance>(GEngine);
    Instance->InitializeStandalone();
    struct FCase { const TCHAR* Name; const TCHAR* Asset; FVector2D Size; const TCHAR* Message; };
    const FCase Cases[] = {
        {TEXT("Instruction"), TEXT("WBP_MediaText"), FVector2D(1200, 360), TEXT("Rotate the patient to face away from the wheelchair, then lower them into the seat.")},
        {TEXT("Retry"), TEXT("WBP_MediaText"), FVector2D(1200, 360), TEXT("Try again - grab the belt handles and lift slowly.")},
        {TEXT("Error"), TEXT("WBP_MediaText"), FVector2D(1200, 360), TEXT("Cannot restore a safe seated checkpoint. Stop this attempt and use the recovery menu.")},
        {TEXT("VideoStatus"), TEXT("WBP_MediaText"), FVector2D(1918, 600), TEXT("Transfers From Bed To Wheelchair Using Transfer Belt")},
        {TEXT("WrongAnswer"), TEXT("WBP_WrongAnswerPopup"), FVector2D(1920, 1080), nullptr},
        {TEXT("Continue"), TEXT("WBP_CompletionPanel"), FVector2D(500, 500), nullptr},
        {TEXT("ContinueLarge"), TEXT("WBP_CompletionPanel"), FVector2D(1931, 1085), nullptr}
    };
    FWidgetRenderer Renderer(true, true);
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("GuidedTraining/PopupCaptures");
    IFileManager::Get().MakeDirectory(*Folder, true);
    for (const FCase& Case : Cases)
    {
        const FString Asset = FString::Printf(TEXT("/Game/Project/Blueprints/UI/%s.%s_C"), Case.Asset, Case.Asset);
        UClass* Class = LoadClass<UUserWidget>(nullptr, *Asset);
        UUserWidget* Widget = Class ? CreateWidget<UUserWidget>(Instance, Class) : nullptr;
        if (!TestNotNull(Case.Name, Widget)) continue;
        if (Case.Message)
        {
            UFunction* Set = Widget->FindFunction(TEXT("SetDisplayText"));
            if (!TestNotNull(TEXT("Existing text callback"), Set)) continue;
            FStructOnScope Params(Set);
            FindFProperty<FTextProperty>(Set, TEXT("InText"))->SetPropertyValue_InContainer(Params.GetStructMemory(), FText::FromString(Case.Message));
            Widget->ProcessEvent(Set, Params.GetStructMemory());
        }
        const TSharedRef<SWidget> Slate = Widget->TakeWidget();
        UTextureRenderTarget2D* Target = Renderer.DrawWidget(Slate, Case.Size);
        // Auto-wrap resolves against the allocated width after the first layout.
        Renderer.DrawWidget(Target, Slate, Case.Size, 0.f);
        Renderer.DrawWidget(Target, Slate, Case.Size, 0.f);
        Widget->WidgetTree->ForEachWidget([&](UWidget* Child)
        {
            UTextBlock* Text = Cast<UTextBlock>(Child);
            if (!Text || Text->GetText().IsEmpty()) return;
            const FGeometry Geometry = Text->GetCachedGeometry();
            const FVector2D Origin = Geometry.GetAbsolutePosition();
            const FVector2D Extent = Geometry.GetLocalSize();
            const FVector2D Desired = Text->GetDesiredSize();
            AddInfo(FString::Printf(TEXT("%s/%s text=(%.1f,%.1f %.1fx%.1f) desired=%.1fx%.1f"), Case.Name, *Text->GetName(), Origin.X, Origin.Y, Extent.X, Extent.Y, Desired.X, Desired.Y));
            TestTrue(TEXT("Complete text allocation lies within render surface"), Origin.X >= -.5f && Origin.Y >= -.5f && Origin.X + Extent.X <= Case.Size.X + .5f && Origin.Y + Extent.Y <= Case.Size.Y + .5f);
            TestTrue(TEXT("Wrapped text has enough vertical space"), Desired.Y <= Extent.Y + .5f);
            for (UWidget* Parent = Text->GetParent(); Parent; Parent = Parent->GetParent())
                if (Cast<UBorder>(Parent))
                {
                    const FGeometry Background = Parent->GetCachedGeometry();
                    const FVector2D Local = Background.AbsoluteToLocal(Origin);
                    const FVector2D Size = Background.GetLocalSize();
                    TestTrue(TEXT("Text remains inside its background"), Local.X >= -.5f && Local.Y >= -.5f && Local.X + Extent.X <= Size.X + .5f && Local.Y + Extent.Y <= Size.Y + .5f);
                    const float Overflow = FMath::Max(0.f, Desired.X - Extent.X) * .5f;
                    TestTrue(TEXT("First and last centred glyphs fit inside background"), Local.X - Overflow >= -.5f && Local.X + Extent.X + Overflow <= Size.X + .5f);
                    break;
                }
        });
        FBufferArchive PNG;
        if (FImageUtils::ExportRenderTarget2DAsPNG(Target, PNG))
            FFileHelper::SaveArrayToFile(PNG, *(Folder / (FString(Case.Name) + TEXT(".png"))));
        else AddError(TEXT("Could not export rendered popup capture"));
    }
    Instance->Shutdown();
    return true;
}
#endif
