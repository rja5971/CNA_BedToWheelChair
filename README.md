# CNA Bed To Wheelchair - Patient Transfer System

## Overview
This project is a VR training simulation focused on physically lifting and transferring a ragdoll patient between a bed and a wheelchair using a transfer belt. It heavily relies on Unreal Engine's physics and skeletal mesh systems to simulate realistic weight and patient handling mechanics.

## Core Systems
*   **Patient Ragdoll Physics (`PatientPhysicsComponent`)**: Handles capturing and freezing the patient's physics state into specific poses (e.g., Lying Down, Seated) by pinning Anchored and Pivot bones in world space.
*   **Transfer Belt (`BeltActor`, `BeltComponent`)**: A physical tool that can be grabbed in VR. When brought near a patient, it auto-attaches to the spine, turning the belt handles into primary lift points for the entire patient ragdoll.
*   **VR Grab System (`GrabComponent`, `IGrabbable`)**: A custom C++ grip component that uses `UPhysicsHandleComponent` to interface with heavy ragdolls, implementing dynamic rotation constraints so patients dangle naturally when lifted instead of rigidly matching the user's wrist.
*   **Seated State Transition (`SeatedTransitionComponent`)**: Analyzes the torso angle of the patient over time when placed on the bed, seamlessly freezing their simulating bones into an upright posture when they successfully balance.

## Development Log
Detailed logs of system architecture, bug fixes, and physics experiments (including the snap-to-sleep bug and local-space dragging loops) are maintained in `devlog.md`.
