# CNA Bed To Wheelchair - Patient Transfer System

## Overview
This project is a VR training simulation focused on physically lifting and transferring a ragdoll patient between a bed and a wheelchair using a transfer belt. It heavily relies on Unreal Engine's physics and skeletal mesh systems to simulate realistic weight and patient handling mechanics.

## Core Systems
*   **Patient Ragdoll Physics (`PatientPhysicsComponent`)**: Handles capturing and freezing the patient's physics state into specific poses (e.g., Lying Down, Seated) by pinning Anchored and Pivot bones in world space.
*   **Transfer Belt (`BeltActor`, `BeltComponent`)**: A physical tool that can be grabbed in VR. When brought near a patient, it auto-attaches to the spine, turning the belt handles into primary lift points for the entire patient ragdoll.
*   **VR Grab System (`GrabComponent`, `IGrabbable`)**: A custom C++ grip component that uses `UPhysicsHandleComponent` to interface with heavy ragdolls, implementing dynamic rotation constraints so patients dangle naturally when lifted instead of rigidly matching the user's wrist.
*   **Seated State Transition (`SeatedTransitionComponent`)**: Keeps bed seating physics-driven and locks the upright pose through `DA_State_Seated`. Final chair placement snaps the pelvis to the selected chair's `SeatTarget`, then blends gradually from physics into `/Game/Animations/AN_Patient_Sitting`.
*   **Multi-Chair Release Handoff**: Every `WheelchairActor` owns an independent `SeatZone` and `SeatTarget`. Releasing the final belt handle inside any ready chair's seat area selects that chair and starts the seating handoff immediately; no single manager reference limits the destination.

## Development Log
Detailed local development notes are maintained in the ignored `devlog.md`; committed architecture and migration guidance lives under `docs/`.
