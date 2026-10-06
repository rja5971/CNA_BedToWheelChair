# CNA Bed To Wheelchair - Patient Transfer System

A high-fidelity Unreal Engine 5.5 VR training simulation designed for Certified Nursing Assistant (CNA) education, guiding healthcare trainees through a safe, belt-assisted patient transfer from a hospital bed into a wheelchair.

---

## Overview

The transfer simulation combines physically simulated patient ragdoll interactions with smooth animation transitions:
- **Bed Preparation**: Physics-driven patient interaction allowing realistic physical support and bed sit-up.
- **Belt Carry**: Kinematic billboard carrying driven by motion controller handles, maintaining patient stability without physics tearing or rubber-banding.
- **Wheelchair Seating**: Multi-chair detection, approach latching, and direct animated seating alignment (`SittingIdle_1__UE`).

---

## Core Systems & Architecture

### 1. Patient Physics (`PatientPhysicsComponent`, `PatientActor`)
- Owns state-driven physical behaviors: `Anchored`, `Pivot`, `Stiff`, and `Free`.
- Dynamic muscle relaxation: When grabbed, physical animation motors are adjusted to allow a stiff physics handle to smoothly lift the 70kg patient model without solver explosions.
- Safe physics recovery: Enforces orientation sanity checks and restore grace periods when interactions end.

### 2. Transfer Belt (`BeltActor`, `BeltComponent`)
- Automatically aligns and attaches to the patient's spine bone.
- Exposes dual VR grab handles for single-hand or dual-hand carrying.
- Provides lifecycle callbacks that drive the transfer state machine.

### 3. Kinematic Billboard Carry (`PatientCarryComponent`)
- Loops an upright patient carry animation while temporarily disabling ragdoll physics simulation.
- Aligns the belt handle to the active VR hand anchor every frame.
- Automatically calculates smooth yaw rotation around world Z to face the VR headset, ensuring natural positioning while carrying.
- Physics-handle carry remains as a safe fallback if no compatible animation is assigned.

### 4. VR Grabbing & Input Routing (`GrabComponent`, `VRWidgetInputComponent`, `IGrabbable`)
- Implements `IGrabbable` across all interactive actors (patient, belt handles, UI widgets).
- Enhanced Input integration (`IMC_Menu`, `IMC_UIInteract`) registered at startup to ensure OpenXR action set creation on Meta Quest.
- Both controller triggers use a calibrated 0.5 activation threshold with explicit press/release pairing to eliminate stuck inputs.

### 5. Seated Transition & Wheelchair Handoff (`SeatedTransitionComponent`, `WheelchairActor`)
- Detects torso upright angles on the bed, triggers the bed seated settle, and runs the fade sequence.
- **Two-Zone Recognition**: Each wheelchair defines an oriented `ApproachZone`, a tighter `SeatZone`, and an exact `SeatTarget`.
- Entering a ready chair's approach zone latches it; releasing the belt within the commit zone transitions the patient directly into chair seating.
- Final seating uses direct `AnimationSingleNode` playback of `/Game/Animations/SittingIdle_1__UE`, cleanly aligning the pelvis transform to `SeatTarget`.

---

## Interaction Lifecycle & Permissions

To prevent physics desynchronization and invalid player actions during key stages, interaction permissions are controlled via `EPatientInteractionPhase`:

| Phase | Description | Patient Grabs | Belt Grabs |
|---|---|:---:|:---:|
| **`BedPreparation`** | Initial patient positioning and sit-up | Allowed (Head/Neck) | Allowed (Attachment rules) |
| **`BedSeating`** | Upright bed settle, pre-fade, and screen fade | **Locked** | **Locked** |
| **`BeltTransfer`** | Post-rotation, belt attachment, and carry | **Locked** | Allowed |
| **`WheelchairSeating`** | Transferring and settling into wheelchair | **Locked** | **Locked** |
| **`Complete`** | Successfully seated in wheelchair | **Locked** | **Locked** |

- Transitioning phases cancels active hand grabs on locked objects and clears hand ownership safely.
- Completion remains locked after score/task updates to prevent disrupting the final seated patient.

---

## Validated Runtime Flow

```text
[Idle] 
  ↓ (Support head/neck)
[Bed Preparation & Sit-Up] 
  ↓ (Torso crosses upright threshold)
[Bed Seated Settle & Fade] 
  ↓ (Rotate patient to bed edge)
[Gait Belt Attachment] 
  ↓ (Grab handles & lift)
[Kinematic Belt Carry] 
  ↓ (Approach wheelchair)
[Chair Approach Recognition] 
  ↓ (Release handles in Seat Commit Zone)
[Wheelchair Seated Animation] 
  ↓
[Transfer Complete]
```

---

## UI, Menu & Media Systems

- **Menu Cursor Routing (`UMenuCursorInteraction`)**: The in-game pause/recovery menu features a dedicated cursor dot centered on initialization. The opening hand (left or right) exclusively owns thumbstick navigation and trigger clicks for that session.
- **Media Reliability (`TrainingMediaControllerComponent`)**: Controls shared training display playback with a 10-second startup deadline, 5-second stall detection, automatic retry budgets, and a native retry widget to handle device video decoder delays.
- **Patient Conversation Toggle**: `BP_PatientChat` contains an editable `bEnablePatientConversation` flag. When disabled, the actor cleanly hides UI panels and skips conversation prompts to allow direct physical transfer training without blocking.

---

## Project Structure

```text
Source/
  HandlingRagdolls/
    Components/       # Belt, Grab, PatientCarry, SeatedTransition components
    Interfaces/       # IGrabbable and interaction interfaces
    Patient/          # APatientActor, physics profiles, interaction phases
    Reliability/      # VR input component, menu cursor, media playback recovery
    StateMachine/     # Transfer workflow and state management
    Transfer/         # ABeltActor, TransferManagerActor
  HandlingRagdollsEditor/
    # Commandlets, asset inspectors, and editor automation tests
Config/
  DefaultGame.ini     # Project metadata and packaging directories
  DefaultInput.ini    # Enhanced Input bindings and OpenXR action mappings
Content/
  Project/            # Blueprints, Maps (CNA_Map_01), Media, and Materials
  VRTemplate/         # VR Pawn, Motion Controllers, Input Contexts
```

---

## Testing & Automation

The project includes automated validation tests covering input routing, media playback recovery, and patient transfer integrity:

- `CNA.Reliability.Input.MenuCursorRouting`: Verifies cursor dot initialization, dedicated virtual user routing, and opposite-hand lockout.
- `CNA.Reliability.Input.OpenXRStartupRegistration`: Confirms Enhanced Input registration priority and trigger binding validation.
- `CNA.Reliability.Media.RecoveryPolicy`: Tests media startup timeouts, stall recovery, and retry behavior.
- `CNA.Patient.Interaction`: Validates phase-based grab locking and transfer state transitions.

Tests can be executed via the Unreal Engine Session Frontend or via commandlet:
```powershell
UnrealEditor-Cmd.exe "<ProjectPath>/CNABedToWheelchair.uproject" -ExecCmds="Automation RunTests CNA.; Quit" -stdout -nullrhi
```
