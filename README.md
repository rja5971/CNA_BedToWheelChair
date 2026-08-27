# CNA Bed To Wheelchair - Patient Transfer System

## Overview

This Unreal Engine 5.5 VR training simulation guides a belt-assisted patient transfer from bed to wheelchair. Bed preparation remains physics-driven; belt carrying and final wheelchair seating use stable animation-owned modes.

## Core Systems

- **Patient physics (`PatientPhysicsComponent`)**: Owns state-driven Anchored, Pivot, Stiff, and Free body behavior, physical-animation profiles, mass, damping, and safe physics recovery.
- **Transfer belt (`BeltActor`, `BeltComponent`)**: Auto-attaches to the patient's configured spine bone and provides the VR grab lifecycle for carrying and final release.
- **Kinematic billboard carry (`PatientCarryComponent`)**: Validates and loops an upright carry animation, disables patient body simulation, aligns the belt handle to the active VR hand anchor every frame, and rotates the whole patient around world Z to face the actual headset. The former physics-handle carry remains the fallback when no compatible animation is assigned.
- **VR grabbing (`GrabComponent`, `IGrabbable`)**: VR interaction leverages a highly-stiff `UPhysicsHandleComponent` combined with dynamic muscle relaxation. When grabbed, the patient's physical animation motors are temporarily disabled (Limp), allowing a 100,000-stiffness physics handle to smoothly lift a 70kg patient without physics tearing or rubber-banding.
- **Seated transition (`SeatedTransitionComponent`)**: Keeps bed seating physics-driven. Final wheelchair release disables ragdoll control, aligns the animated pelvis exactly to the selected chair's `SeatTarget`, applies the calibrated `-180 degree` skeletal yaw, and plays `/Game/Animations/SittingIdle_1__UE` at full weight.
- **Direct seated animation**: Final seating uses `AnimationSingleNode` playback of `/Game/Animations/SittingIdle_1__UE`; there is no seated animation blueprint or foot-IK layer in the runtime path.
- **Two-zone multi-chair handoff**: Every `WheelchairActor` owns an oriented `ApproachZone`, smaller `SeatZone`, and exact `SeatTarget`. Entering a ready chair's approach area latches it immediately; releasing the final belt handle in its commit zone starts seating. Duplicate or unavailable chairs cannot steal selection, and release is consumed only after a valid match.

## Validated Runtime Flow

`Idle -> Neck Support -> Belt Attach -> Kinematic Belt Carry -> Ready-Chair Recognition -> Release in Seat Commit Zone -> Seated Animation -> Complete`

Billboard carrying, re-grab/release, oriented chair recognition, rotated-chair detection,
release-driven direct seated playback, and pelvis attachment to `SeatTarget` have passed
VR testing. Releasing away from a valid chair also restores patient physics after the
carry grace period. `CNABedToWheelchairEditor Win64 Development` compiles and links
successfully.

## Documentation

- Architecture: `docs/RAGDOLL_ARCHITECTURE.md`
- Setup and migration: `docs/RAGDOLL_MIGRATION.md`
- Meta Quest client build: `docs/META_QUEST_BUILD.md`
- Development history: `devlog.md`
- VR locomotion: `docs/VR_Locomotion_Guide.md`

## Current Delivery Focus

The validated patient-transfer workflow is now frozen for the first Meta Quest
client build. Final chair seating uses the direct seated animation path.
Quest packaging preparation, cook validation, device installation, and headset
smoke testing are the active delivery tasks.
