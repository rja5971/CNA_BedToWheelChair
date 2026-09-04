// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "../Interfaces/IGrabbable.h"
#include "BeltActor.generated.h"

class UBeltComponent;
class UStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;
class USceneComponent;
class USphereComponent;
class USplineComponent;
class USplineMeshComponent;

/**
 * Belt Actor — the physical transfer belt that the nurse attaches to the patient.
 * 
 * This actor owns the BeltComponent and implements IGrabbable at the actor level.
 * (UActorComponents cannot safely implement UInterfaces in UE5 reflection.)
 * 
 * Workflow:
 * 1. Nurse picks up belt (IGrabbable)
 * 2. Nurse brings belt near patient → auto-attaches via overlap detection
 * 3. Once attached, belt handles become the grab targets for lifting
 */
UCLASS(BlueprintType)
class HANDLINGRAGDOLLS_API ABeltActor : public AActor, public IIGrabbable
{
	GENERATED_BODY()

public:
	ABeltActor();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void OnConstruction(const FTransform& Transform) override;

public:
	// ============================================================
	// IGrabbable Implementation
	// ============================================================
	virtual bool CanBeGrabbed(FName BoneName, FVector GrabLocation) const override;
	virtual void OnGrabbed(UGrabComponent* Grabber, FName BoneName, FVector GrabLocation) override;
	virtual void OnReleased(UGrabComponent* Grabber) override;
	virtual UPrimitiveComponent* GetGrabbableComponent() const override;
	virtual TArray<FName> GetGrabbableBoneNames() const override;
	virtual FName GetGrabBoneOverride() const override;
	virtual bool RequiresRotationConstraint() const override;
	virtual bool ShouldUsePhysicsHandle() const override;

	/** Get the belt component */
	UFUNCTION(BlueprintCallable, Category = "Belt")
	UBeltComponent* GetBeltComponent() const { return BeltComp; }
	FVector GetHandleWorldLocation() const;

	/** Begin morphing the loose belt into its fitted waist shape. */
	UFUNCTION(BlueprintCallable, Category = "Belt|Spline")
	void StartWrapAnimation();

	/** Return the visual belt to its loose pickup shape after detaching. */
	UFUNCTION(BlueprintCallable, Category = "Belt|Spline")
	void ResetWrapAnimation();

	/** The fitted belt cannot be used as a lift handle until wrapping has finished. */
	UFUNCTION(BlueprintPure, Category = "Belt|Spline")
	bool IsWrapComplete() const { return bWrapComplete; }

protected:
	/** The belt mesh */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Belt")
	TObjectPtr<UStaticMeshComponent> BeltMesh;

	/** Front handle grab point */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Belt")
	TObjectPtr<USceneComponent> HandleFront;

	/** Front handle visual indicator (sphere) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Belt")
	TObjectPtr<UStaticMeshComponent> HandleFrontVisual;

	/** Offset of the front handle from the belt center (tweak to position it in front of the patient) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Belt|Config")
	FVector HandleOffset = FVector(40.0f, 0.0f, 0.0f);

	/** The belt logic component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Belt")
	TObjectPtr<UBeltComponent> BeltComp;

	/** Proximity detection sphere — visual reference for attach range */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Belt")
	TObjectPtr<USphereComponent> AttachProximity;

	/** Distance (in cm) at which the belt auto-attaches to the patient while being carried */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Belt|Detection",
		meta = (ClampMin = "10.0", ClampMax = "500.0"))
	float AttachRadius = 75.0f;

	/** Show the detection sphere in-game for debugging attachment range */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Belt|Detection")
	bool bShowDetectionRadius = false;

	/** Spline component used to generate the belt mesh */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Belt|Spline")
	TObjectPtr<USplineComponent> BeltSpline;

	/** Mesh segment to repeat along the spline */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Belt|Spline")
	TObjectPtr<UStaticMesh> SplineSegmentMesh;

	/** Material for the spline mesh (can be used for UV panning animation) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Belt|Spline")
	TObjectPtr<UMaterialInterface> SplineSegmentMaterial;

	/** Generated spline mesh components */
	UPROPERTY()
	TArray<TObjectPtr<USplineMeshComponent>> RuntimeSplineMeshes;

	/** Number of reusable mesh sections around the belt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Belt|Spline",
		meta = (ClampMin = "8", ClampMax = "32"))
	int32 SplineSegmentCount = 16;

	/** Time taken to move from the loose loop to the fitted waist shape. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Belt|Spline|Animation",
		meta = (ClampMin = "0.1", ClampMax = "5.0", Units = "s"))
	float WrapDuration = 1.35f;

	/** Patient-front/back radius of the final fitted ellipse. Local X is patient forward. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Belt|Spline|Fitted Shape",
		meta = (ClampMin = "5.0", ClampMax = "75.0", Units = "cm"))
	float WaistHalfDepth = 23.0f;

	/** Patient left/right radius of the final fitted ellipse. Local Y is patient right. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Belt|Spline|Fitted Shape",
		meta = (ClampMin = "5.0", ClampMax = "75.0", Units = "cm"))
	float WaistHalfWidth = 34.0f;

	/** Local height adjustment for the final belt contour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Belt|Spline|Fitted Shape",
		meta = (ClampMin = "-30.0", ClampMax = "30.0", Units = "cm"))
	float WaistVerticalOffset = 0.0f;

	/** How far in front of the waist the loose belt initially hangs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Belt|Spline|Loose Shape",
		meta = (ClampMin = "5.0", ClampMax = "100.0", Units = "cm"))
	float LooseForwardOffset = 42.0f;

	/** Front/back depth of the loose hanging loop. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Belt|Spline|Loose Shape",
		meta = (ClampMin = "1.0", ClampMax = "50.0", Units = "cm"))
	float LooseHalfDepth = 12.0f;

	/** Left/right spread of the loose hanging loop. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Belt|Spline|Loose Shape",
		meta = (ClampMin = "5.0", ClampMax = "75.0", Units = "cm"))
	float LooseHalfWidth = 25.0f;

	/** Distance the bottom of the loose loop hangs below its pickup point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Belt|Spline|Loose Shape",
		meta = (ClampMin = "5.0", ClampMax = "100.0", Units = "cm"))
	float LooseDrop = 38.0f;

	/** Cross-section scale for the repeated segment mesh (Y thickness, Z belt width). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Belt|Spline|Mesh")
	FVector2D SplineMeshCrossSectionScale = FVector2D(0.0125f, 0.10f);

	/** Rebuilds the spline meshes based on current spline points */
	UFUNCTION(BlueprintCallable, Category = "Belt|Spline")
	virtual void RebuildSplineMeshes();

	/** Advance the active wrap and update spline deformation. */
	UFUNCTION(BlueprintCallable, Category = "Belt|Spline")
	virtual void AnimateBelt(float DeltaTime);

	void EnsureSplinePointCount();
	void UpdateSplineForProgress(float InProgress);
	void UpdateSplineMeshSegments();
	void SuppressLegacyBlueprintBeltVisuals();
	FVector GetLooseSplinePoint(float AngleRadians) const;
	FVector GetFittedSplinePoint(float AngleRadians) const;

	float WrapElapsed = 0.0f;
	float WrapProgress = 0.0f;
	bool bWrapAnimating = false;
	bool bWrapComplete = false;

private:
	/** Whether the belt is currently being held by the nurse */
	bool bIsBeingCarried = false;
};
