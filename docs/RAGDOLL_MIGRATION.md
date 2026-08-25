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
   - `PatientCarry -> Carry Animation`: an upright in-place loop using the patient skeleton (currently `/Game/Animations/Idle_UE`)
4. On every `WheelchairActor`, assign the CNA wheelchair mesh from `/Game/Project/EventProps/WheelChair` and align its `SeatTarget`, broad `ApproachZone`, and smaller `SeatZone` to the physical chair. Detection uses chair-local space and remains accurate when the chair rotates. Enable `Show Detection Zones` to tune the yellow recognition box and green commit box.
5. Position `LeftFootTarget` and `RightFootTarget` on the two footrests. Place `LeftKneeTarget` and `RightKneeTarget` forward of their matching knees so the Two Bone IK chains bend naturally.
6. Assign the patient, belt, and wheelchair references on `TransferManagerActor`, and enable `Auto Start` if the training should begin immediately.
7. Add one native `GrabComponent` to each hand/controller component on the CNA `VRPawn`. Bind grip pressed to `Try Grab` and grip released to `Release Grab` for the corresponding hand.
8. Add `VRLocomotionSettings` to the CNA `VRPawn` if runtime switching between teleport/smooth move and snap/smooth turn is required. The existing CNA input assets are not replaced.

The runtime flow is:

`Idle -> Neck Support -> Belt Attach -> Belt Lift -> Wheelchair Transfer -> Complete`

When a compatible carry animation is assigned, `UPatientCarryComponent` disables
patient body simulation, aligns the belt handle to the active hand anchor, and
yaw-billboards the upright patient toward the actual VR headset. The former physics
carry remains the automatic fallback if the animation is missing or incompatible.
Entering a ready wheelchair's approach zone latches that chair. Releasing the final
belt handle inside its commit zone aligns the animated pelvis to its target and starts
`/Game/Animations/SittingIdle_1__UE` at full weight in the same frame. Final yaw
uses wheelchair forward with the patient's calibrated `-180°` skeletal-axis offset.

Release is not consumed until both a ready latched chair and valid commit-zone match
exist. Kinematic physics velocity is not used as a seating trigger.

When `/Game/Animations/ABP_Patient_SeatedIK` is assigned, the seated base
animation feeds two component-space Two Bone IK nodes using the chair-owned foot
and knee targets. `FootIKAlpha` blends the correction on after the chair locks.
If that class is absent or unassigned, seating automatically retains the original
single-node `/Game/Animations/SittingIdle_1__UE` path.
The IK path is currently disabled by default through `bEnableSeatedFootIK`; leave
it unchecked until the seated AnimGraph and target placement are ready for testing.

## Important integration note

The classes and data are migrated and compile as part of this project, but final placement and hand-input wiring are level/Blueprint choices specific to the CNA training flow. Do not replace the CNA `VRPawn` or project maps with the old Ragdoll template versions.

For the current client delivery baseline and on-device validation requirements, see
`docs/META_QUEST_BUILD.md`.
