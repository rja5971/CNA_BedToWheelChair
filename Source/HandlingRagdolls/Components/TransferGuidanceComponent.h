#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "../Patient/PatientTypes.h"
#include "../StateMachine/TransferTaskState.h"
#include "TransferGuidanceComponent.generated.h"

class UTransferStateMachine;
class APatientActor;
class ABeltActor;
class AWheelchairActor;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class USkeletalMesh;
class UAnimSequence;
class UStaticMesh;
class UGrabComponent;

UENUM(BlueprintType)
enum class ETransferHintStep : uint8
{
    None, GrabPatient, SitUp, PickUpBelt, AttachBelt, GrabBeltHandle, LiftAndCarry, MoveToSeat, ReleaseToSeat
};

/** Session preference survives map travel and resets to On at application launch. */
UCLASS()
class HANDLINGRAGDOLLS_API UTransferHintsSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintPure, Category="CNA|Hints") bool AreHintsEnabled() const { return bEnabled; }
    UFUNCTION(BlueprintCallable, Category="CNA|Hints") void SetHintsEnabled(bool bValue) { bEnabled = bValue; }
private:
    bool bEnabled = true;
};

/** Local grab poses and world-space lift/label offsets, in centimetres. */
USTRUCT(BlueprintType)
struct FTransferGuidanceVisualSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector SitUpDirection = FVector(0, 0, 40);
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector LiftDirection = FVector(0, 0, 25);
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector LabelOffset = FVector(0, 0, 22);
    /** Floating handle text only: X toward viewer, Y viewer's right, Z up, in centimetres. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(Units="cm")) FVector BeltHandleLabelOffset = FVector(40, 0, 22);
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector ChairLabelOffset = FVector(0, 0, 85);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.5")) float LoopDuration = 2.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1")) float TextSize = 4.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", ClampMax="1")) float GhostOpacity = .45f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FLinearColor HintColor = FLinearColor(0.f, .45f, .65f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FLinearColor ReadyColor = FLinearColor(.15f, 1.f, .25f);
};

/** Read-only rendering observer; never owns interaction or changes the transfer. */
UCLASS(ClassGroup=(PatientCare), meta=(BlueprintSpawnableComponent))
class HANDLINGRAGDOLLS_API UTransferGuidanceComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UTransferGuidanceComponent();
    void Initialize(UTransferStateMachine* InStateMachine);
    UFUNCTION(BlueprintCallable, Category="CNA|Hints") void SetHintsEnabled(bool bEnabled);
    UFUNCTION(BlueprintPure, Category="CNA|Hints") bool AreHintsEnabled() const;
    UFUNCTION(BlueprintPure, Category="CNA|Hints") ETransferHintStep GetHintStep() const { return Step; }
    UFUNCTION(BlueprintPure, Category="CNA|Hints") AWheelchairActor* GetSelectedWheelchair() const { return SelectedChair.Get(); }
    UFUNCTION(BlueprintPure, Category="CNA|Hints") FText GetHintText() const;
    void RefreshHints();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CNA|Hints") FTransferGuidanceVisualSettings VisualSettings;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    friend class FCNATransferGuidanceTest;
    void CreateVisuals();
    void UpdateVisuals();
    void HideVisuals();
    void BindEvents();
    void UnbindEvents();
    void BindHandEvents();
    UFUNCTION() void OnHandGrab(AActor* Actor, FName Bone, FVector Location);
    UFUNCTION() void OnHandRelease(AActor* Actor);
    UFUNCTION() void OnPatientState(EPatientState State);
    UFUNCTION() void OnTransferState(ETransferState OldState, ETransferState NewState);
    UFUNCTION() void OnTransferFailed(FText Reason);
    UFUNCTION() void OnBeltAttached(AActor* Patient);
    UFUNCTION() void OnBeltDetached();
    UFUNCTION() void OnHandleGrabbed(FName Handle, AActor* Owner);
    bool IsMenuOpen() const;
    bool IsActorHeld(AActor* Actor) const;
    FTransform GrabTarget() const;
    UPROPERTY(Transient) TObjectPtr<UTransferStateMachine> StateMachine;
    UPROPERTY(Transient) TObjectPtr<APatientActor> BoundPatient;
    UPROPERTY(Transient) TObjectPtr<ABeltActor> BoundBelt;
    UPROPERTY(Transient) TObjectPtr<USkeletalMeshComponent> SeatedGhost;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Target;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> BeltGhost;
    UPROPERTY(Transient) TArray<TObjectPtr<UGrabComponent>> BoundHands;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> ArrowShaft;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> ArrowHead;
    UPROPERTY(Transient) TObjectPtr<UTextRenderComponent> Label;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> GhostMID;
    UPROPERTY() TObjectPtr<UMaterialInterface> GhostMaterial;
    UPROPERTY() TObjectPtr<UMaterialInterface> TextOutlineMaterial;
    UPROPERTY() TObjectPtr<UAnimSequence> ChairPose;
    UPROPERTY() TObjectPtr<UStaticMesh> SphereMesh;
    UPROPERTY() TObjectPtr<UStaticMesh> CylinderMesh;
    UPROPERTY() TObjectPtr<UStaticMesh> ConeMesh;
    TWeakObjectPtr<AWheelchairActor> SelectedChair;
    ETransferHintStep Step = ETransferHintStep::None;
    float RefreshElapsed = 0.f;
    float LoopElapsed = 0.f;
    bool bLocalEnabled = true;
    bool bTransferFailed = false;
};
