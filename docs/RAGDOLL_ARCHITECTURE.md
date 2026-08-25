# VR Patient Care & Locomotion Simulator: Architecture Deep Dive

> **Primary Technical Reference Document**
> Repository Location: `docs/ARCHITECTURE_DEEP_DIVE.md`

---

## 1. Project System Architecture Map

> **Implemented as a SOLID C++-first system.** The patient uses a hybrid physical
> animation approach, and transfers are belt-assisted (patient cannot walk).

```
+-----------------------------------------------------------------------------------+
|                                  PLAYER (NURSE)                                   |
|  BP_VRPawn (ACharacter)                                                           |
|  ├── CameraComponent                                                              |
|  ├── LeftMotionController / RightMotionController                                 |
|  ├── UGrabComponent  x2   (C++ — physics-handle grab, works via IGrabbable)       |
|  └── UVRLocomotionSettings (C++ Component)                                        |
+-----------------------------------------------------------------------------------+
                                      │  TryGrab() → IGrabbable
                                      ▼
+-----------------------------------------------------------------------------------+
|                               PATIENT (MANNEQUIN)                                 |
|  APatientActor (AActor)  : IGrabbable, IBeltAttachable, ISpineMonitorable, IPatient|
|  ├── USkeletalMeshComponent (PA_Patient_SpineRestricted)                           |
|  ├── UPhysicalAnimationComponent  (raw engine physical animation)                 |
|  ├── UPatientPhysicsComponent     (physics lifecycle: profiles, mass, damping,    |
|  │                                 ApplyStateConfig — data-driven bone behavior)  |
|  ├── USeatedTransitionComponent   (pure-physics sit detection + settle + freeze)  |
|  ├── UCooperationRampComponent    (progressive aliveness during fold-up)          |
|  └── UPatientCarryComponent       (animated hand follow + headset billboard yaw)  |
|                                                                                   |
|  Data-driven behavior via UPatientStateConfig assets (StateConfigs array):        |
|    each state → per-bone-group behavior (Anchored / Stiff / Free)                  |
+-----------------------------------------------------------------------------------+
        │  IBeltAttachable                         │  ITransferTarget
        ▼                                          ▼
+------------------------------------+   +--------------------------------------+
|  BELT                              |   |  ENVIRONMENT                         |
|  ABeltActor (AActor) : IGrabbable  |   |  AWheelchairActor : ITransferTarget  |
|  └── UBeltComponent (attach/lift)  |   |  ├── StaticMeshComponent             |
|      attached carry handle         |   |  ├── ApproachZone (recognition)      |
+------------------------------------+   |  ├── SeatZone (commit)               |
                                         |  └── SeatTarget (exact pelvis pose)  |
                                         +--------------------------------------+
```

## 1a. Component Architecture (post-refactor)

The patient was refactored from a 700-line god class into focused components (SRP):

| Component | Responsibility |
|-----------|---------------|
| `APatientActor` | Thin coordinator; implements interfaces; state routing; spine stress |
| `UPatientPhysicsComponent` | Physics mode, profiles, mass, damping, `ApplyStateConfig()` |
| `USeatedTransitionComponent` | Bed sit detection plus targeted physics-to-animation wheelchair handoff |
| `UCooperationRampComponent` | Progressive muscle engagement during fold-up (curve-driven) |
| `UPatientCarryComponent` | Fully kinematic carry animation, averaged hand following, headset-facing yaw, release recovery |
| `UGrabComponent` | VR hand physics-handle grab (via IGrabbable) |
| `USpineMonitorComponent` | Observes ISpineMonitorable, fires stress events |

**Data-driven state system:** `UPatientStateConfig` assets define per-state bone behavior.
Each config lists bone GROUPS (Pelvis, Spine, Neck, Head, Arms, Legs) with a behavior
(Anchored / Stiff / Free). The state machine drives transitions; the config drives physics.
Bed positioning remains physics-driven. Attached-belt carrying becomes animation-owned
when a compatible `CarryAnimation` is configured: body simulation is disabled, the
belt handle follows the active hand anchor, and the upright patient billboard-yaws
toward the headset without inheriting wrist rotation. The previous physics path is
retained as an asset-validation fallback. Once the patient is released inside a
latched ready wheelchair's oriented `SeatZone`, the animated pelvis
snaps exactly to that chair's `SeatTarget`, and `/Game/Animations/AN_Patient_Sitting`
takes full control in the same frame. `SeatTarget` supplies the pelvis position;
the wheelchair actor supplies final facing. A calibrated `-180°` yaw compensates
for the imported patient's opposite skeletal forward axis, so a rotated target
component cannot seat the patient backward.

Direct patient grabbing is state-gated. `LyingDown`, `BeingSupported`, and bed
`Seated` accept only configured neck-support bones; generic physics bodies cannot
bypass this rule. Startup also always applies the current state data asset instead
of allowing a global limp-test option to skip its Anchored/Stiff/Free behaviors.

**State machine decoupling:** `UTransferStateMachine` holds `TScriptInterface<IIPatient>`,
not a concrete `APatientActor*`. State sequence is a data-driven `TArray<ETransferState>`.
```

+-----------------------------------------------------------------------------------+
|                        ORCHESTRATION (on a Game Manager)                          |
|  UTransferStateMachine (C++ Component)                                            |
|    Idle → NeckSupport → BeltAttach → BeltLift → WheelchairTransfer → Complete     |
|    (each step is a UTransferTaskState subclass — Open/Closed)                     |
|  UScoringComponent — score / penalties / grade / pass-fail                        |
+-----------------------------------------------------------------------------------+
```

---

## 2. Completed Locomotion Subsystem Deep Dive

### 2.1 C++ Class: `UVRLocomotionSettings`
- **Location:** `Source/HandlingRagdolls/VRLocomotionSettings.h` & `.cpp`
- **Class Group:** `Custom`, `meta=(BlueprintSpawnableComponent)`
- **Header Structure:**
  ```cpp
  #pragma once
  #include "CoreMinimal.h"
  #include "Components/ActorComponent.h"
  #include "VRLocomotionSettings.generated.h"

  UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
  class HANDLINGRAGDOLLS_API UVRLocomotionSettings : public UActorComponent
  {
      GENERATED_BODY()
  public:
      UVRLocomotionSettings();

      UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR Locomotion")
      bool bUseSnapTurn;

      UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR Locomotion")
      bool bUseTeleport;

      UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR Locomotion")
      float ContinuousTurnSpeed;

      UFUNCTION(BlueprintCallable, Category = "VR Locomotion")
      void ApplyTurn(APawn* Pawn, float AxisValue, float DeltaTime);

      UFUNCTION(BlueprintCallable, Category = "VR Locomotion")
      void ApplySmoothMove(APawn* Pawn, FVector2D AxisValue, FVector ForwardDirection, FVector RightDirection);

  private:
      bool bHasSnapTurned;
  };
  ```

### 2.2 Key Configuration Rules for `BP_VRPawn` (Character Class)
- **`bUseControllerRotationYaw`**: MUST BE `FALSE`. (If set to `TRUE`, the Player Controller will lock the character's rotation and override `AddActorLocalRotation`).
- **`CharacterMovementComponent->bOrientRotationToMovement`**: MUST BE `FALSE`. (If set to `TRUE`, sliding against walls will cause violent 90° camera snaps).
- **`IA_Move` Input Action:** Value Type MUST BE `Axis2D (Vector2D)`. Bound in `IMC_Default` to `Oculus Touch (L) Thumbstick 2D-Axis`.
- **`IA_Turn` Input Action:** Value Type MUST BE `Axis1D (float)`. Bound in `IMC_Default` to `Oculus Touch (R) Thumbstick X-Axis`. Deadzone set to `0.2`, `Triggers` array MUST BE EMPTY (0 elements).
- **`TryTeleport` Z-Offset:** Must add `+88.0` units to the Z-axis of `Projected Teleport Location` before calling `TeleportTo` to offset the `CapsuleComponent` height.

---

## 3. Patient Care & Ragdoll Subsystem Deep Dive

### 3.1 Physics Asset Tuning (`PA_Patient_SpineRestricted`)
- **Bone Hierarchy:** `pelvis` -> `spine_01` -> `spine_02` -> `spine_03` -> `neck_01` -> `head`.
- **Spine Joint Constraints:**
  - Angular Swing 1 Motion: `ACM_Limited` (10.0°)
  - Angular Swing 2 Motion: `ACM_Limited` (10.0°)
  - Angular Twist Motion: `ACM_Limited` (5.0°)
  - Angular Damping: `50.0`
  - Stiffness: `500.0`
- **Purpose:** Prevents unnatural hyperextension or twisting of the patient's lumbar and thoracic spine when lifted by the nurse.

### 3.2 C++ Class: `APatientActor` (implements the actual spine monitoring)

> Supersedes the earlier single `UPatientSpineMonitor` plan. Spine monitoring is
> split between `APatientActor` (data via `ISpineMonitorable`) and
> `USpineMonitorComponent` (event emission), following SOLID.

- **Location:** `Source/HandlingRagdolls/Patient/PatientActor.h` / `.cpp`
- **Interfaces implemented:** `IGrabbable`, `IBeltAttachable`, `ISpineMonitorable`.
- **Spine stress method (per-bone quaternion deviation):**
  Each tick, for every bone in `USpineConstraintConfig::SpineBones`, compute the
  angular deviation of the bone's current component-space rotation from its cached
  rest-pose rotation:
  ```
  DeltaQuat = RestQuat.Inverse() * CurrentQuat
  DeviationDeg = DeltaQuat.ToAxisAndAngle().Angle (converted to degrees)
  ```
  If `DeviationDeg > SafeSwingAngle`, accumulate stress scaled by the excess ratio,
  the bone's `DamageMultiplier`, and `StressAccumulationRate`. Otherwise stress
  decays by `StressDecayRate`. When total `AccumulatedDamage >= DamageThreshold`,
  the patient transitions to `Injured` and `OnPatientInjured` fires.
- **Hybrid Physical Animation:**
  - `UPhysicalAnimationComponent` bound to the mesh in `BeginPlay`.
  - `EnablePhysicalAnimation()` — motor-driven bodies (conscious patient).
  - `EnableRagdoll()` — full limp (injured/unconscious).
  - `ApplyPhysicalAnimProfile(EPhysicalAnimProfile)` — Relaxed / Cooperating / Seated / Limp.
  - `SetPatientState()` auto-selects the profile for each state.

### 3.3 Supporting Components & Actors
- **`USpineMonitorComponent`** — polls the owner's `ISpineMonitorable` each tick,
  broadcasts `OnSpineWarning`, `OnSpineCritical`, `OnSpineFailure`, `OnSpineSafe`.
- **`UGrabComponent`** — on VR hands. `TryGrab()` sphere-traces for `IGrabbable`,
  grabs via an internal `UPhysicsHandleComponent`, tracks the hand each tick.
- **`UBeltComponent` + `ABeltActor`** — belt attaches directly to the configured
  patient skeletal bone. Attached grabs delegate transport to `UPatientCarryComponent`
  when its animation is valid; otherwise they retain the physics-handle fallback.
- **`AWheelchairActor`** — `ITransferTarget`; each instance owns an independent
  oriented `ApproachZone`, `SeatZone`, and `SeatTarget`. Ready chairs are recognized
  and latched in the approach zone; duplicate/unavailable chairs cannot steal
  selection, and final release is consumed only after a commit-zone match.
- **`UTransferStateMachine`** — orchestrates the 6-state flow; each state is a
  `UTransferTaskState` subclass with its own entry/tick/transition/failure logic.
- **`UScoringComponent`** — accumulates penalties (spine stress, dropping too fast,
  losing neck support) and rewards (correct step order, two-hand belt grip); grades A–F.

---

## 4. VR Hand Physics Handle Mechanism (`UGrabComponent`)
- **Left/Right Hands:** Each equipped with a `UGrabComponent` (which internally
  creates and manages a `UPhysicsHandleComponent`).
- **Grip Process:**
  1. Grip button → `UGrabComponent::TryGrab()`.
  2. `TryGrab()` sphere-traces within `GrabRadius` for actors implementing `IGrabbable`.
  3. Calls `IGrabbable::CanBeGrabbed(Bone, Location)` to validate the target.
  4. Grabs the returned primitive via `GrabComponentAtLocationWithRotation(...)`.
  5. Each tick, `SetTargetLocationAndRotation(...)` follows the hand.
  6. The grabbed actor is notified via `IGrabbable::OnGrabbed(...)`.
- **Two grab phases:**
  - **Phase 1 (neck support):** grab the patient directly on `neck_01` / head bones.
  - **Phase 2 (lift):** grab `BeltHandle_L` / `BeltHandle_R` on the attached `ABeltActor`.
- During kinematic Phase 2, no physics handle is created. Grab events instead drive
  `UPatientCarryComponent`, which updates the whole animated mesh after pose evaluation.
- Stiffness/damping are exposed on `UGrabComponent` (`GrabLinearStiffness`,
  `GrabAngularStiffness`, etc.) to tune the "heavy body" weight feel.
