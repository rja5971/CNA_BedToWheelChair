# CNA Bed To Wheelchair - Patient Transfer System

## Overview

This Unreal Engine 5.5 VR training simulation guides a belt-assisted patient transfer from bed to wheelchair. Bed preparation remains physics-driven; belt carrying and final wheelchair seating use stable animation-owned modes.

## Core Systems

- **Patient physics (`PatientPhysicsComponent`)**: Owns state-driven Anchored, Pivot, Stiff, and Free body behavior, physical-animation profiles, mass, damping, and safe physics recovery.
- **Transfer belt (`BeltActor`, `BeltComponent`)**: Auto-attaches to the patient's configured spine bone and provides the VR grab lifecycle for carrying and final release.
- **Kinematic billboard carry (`PatientCarryComponent`)**: Validates and loops an upright carry animation, disables patient body simulation, aligns the belt handle to the active VR hand anchor every frame, and rotates the whole patient around world Z to face the actual headset. The former physics-handle carry remains the fallback when no compatible animation is assigned.
- **VR grabbing (`GrabComponent`, `IGrabbable`)**: Normally uses `UPhysicsHandleComponent`, but supports kinematic interactions that receive grab/release events without creating a competing physics handle.
- **Seated transition (`SeatedTransitionComponent`)**: Keeps bed seating physics-driven. Final wheelchair release disables ragdoll control, aligns the animated pelvis exactly to the selected chair's `SeatTarget`, applies the calibrated `-180 degree` skeletal yaw, and plays `/Game/Animations/AN_Patient_Sitting` at full weight.
- **Two-zone multi-chair handoff**: Every `WheelchairActor` owns an oriented `ApproachZone`, smaller `SeatZone`, and exact `SeatTarget`. Entering a ready chair's approach area latches it immediately; releasing the final belt handle in its commit zone starts seating. Duplicate or unavailable chairs cannot steal selection, and release is consumed only after a valid match.

## Validated Runtime Flow

`Idle -> Neck Support -> Belt Attach -> Kinematic Belt Carry -> Ready-Chair Recognition -> Release in Seat Commit Zone -> Seated Animation -> Complete`

Billboard carrying, re-grab/release, oriented chair recognition, rotated-chair detection, and final seating have passed VR testing. `CNABedToWheelchairEditor Win64 Development` compiles and links successfully.

## Documentation

- Architecture: `docs/RAGDOLL_ARCHITECTURE.md`
- Setup and migration: `docs/RAGDOLL_MIGRATION.md`
- Development history: `devlog.md`
- VR locomotion: `docs/VR_Locomotion_Guide.md`
