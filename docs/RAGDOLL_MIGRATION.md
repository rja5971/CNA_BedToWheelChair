# Ragdoll patient-transfer system

The reusable system from `HandlingRagdolls` is installed in this project as two native modules:

- `HandlingRagdolls`: runtime patient, belt, wheelchair, grabbing, locomotion, scoring, and transfer-state-machine code.
- `HandlingRagdollsEditor`: patient setup, bone auto-detection, animation generation, and state-config debug tools.

The migrated data assets remain at their original `/Game` package paths so their internal references stay valid:

- `/Game/Data` and `/Game/Data/States`
- `/Game/Patient`
- `/Game/PatientSetup`
- `/Game/Animations`
- `/Game/EditorBlueprint`

The CNA project-owned maps, `VRPawn`, input mappings, packaging settings, and existing Blueprint belt flow were deliberately preserved.

## Add the system to `CNA_Map_01`

1. Build and open the project, then let Unreal compile any affected Blueprints.
2. Place a `PatientActor`, `BeltActor`, one or more `WheelchairActor` instances, and a `TransferManagerActor` in the level.
3. On `PatientActor`, assign:
   - skeletal mesh: `/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple`
   - physics asset: `/Game/Patient/PA_Patient_SpineRestricted`
   - bone mapping: `/Game/PatientSetup/DA_BoneMapping_SKM_Manny_Simple`
   - state configs from `/Game/Data/States`
4. On every `WheelchairActor`, assign the CNA wheelchair mesh from `/Game/Project/EventProps/WheelChair` and align its `SeatTarget` and `SeatZone` to the physical chair. Runtime seating dynamically selects the chair containing the patient's pelvis.
5. Assign the patient, belt, and wheelchair references on `TransferManagerActor`, and enable `Auto Start` if the training should begin immediately.
6. Add one native `GrabComponent` to each hand/controller component on the CNA `VRPawn`. Bind grip pressed to `Try Grab` and grip released to `Release Grab` for the corresponding hand.
7. Add `VRLocomotionSettings` to the CNA `VRPawn` if runtime switching between teleport/smooth move and snap/smooth turn is required. The existing CNA input assets are not replaced.

The runtime flow is:

`Idle -> Neck Support -> Belt Attach -> Belt Lift -> Wheelchair Transfer -> Complete`

The `BeingTransferred` data asset uses pivot behavior: the pelvis position remains
fixed while two belt-hand positions drive patient yaw. Releasing the final belt
handle inside any wheelchair seat zone immediately aligns the pelvis to that
chair's target and begins the gradual blend into
`/Game/Animations/AN_Patient_Sitting`.

## Important integration note

The classes and data are migrated and compile as part of this project, but final placement and hand-input wiring are level/Blueprint choices specific to the CNA training flow. Do not replace the CNA `VRPawn` or project maps with the old Ragdoll template versions.
