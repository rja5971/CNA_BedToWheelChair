# Meta Quest client build

## Delivery scope

The next deliverable is a Meta Quest APK for client review. The build should retain
the VR-tested patient-transfer flow and avoid introducing new gameplay changes
during packaging work. Chair seating uses the validated
`/Game/Animations/SittingIdle_1__UE` single-node path without a seated AnimBlueprint.

## Current project configuration

- Unreal Engine: 5.5
- Target platform declared by the project: Android
- XR runtime: OpenXR
- Android support enabled for OpenXR, OpenXR Eye Tracker, and OpenXR Hand Tracking
- `bPackageForMetaQuest=True`
- Current configured Android target SDK: 33
- Package data inside APK: enabled
- Current packaging configuration: Development
- Distribution signing: disabled
- Start in VR: enabled
- Default game map: `/Game/Project/Maps/TrainingModeSelectionMap`
- Explicitly cooked maps:
  - `/Game/Project/Maps/TrainingModeSelectionMap`
  - `/Game/Project/Maps/CNA_Map_01`

These entries describe the repository state; the installed JDK, Android SDK, NDK,
platform API, device authorization, package identifier, and signing configuration
must still be validated on the build machine before packaging.

## Build preparation checklist

1. Confirm Unreal Engine 5.5 reports a valid Android SDK, NDK, and JDK toolchain.
2. Confirm the Android package name, application display name, version code, and
   version name intended for the client.
3. Confirm the target Meta Quest headset is visible to ADB and authorized for USB
   debugging.
4. Run an Android Development cook/package first and preserve the complete
   AutomationTool log.
5. Install the generated APK on the headset and launch it without the editor.
6. Perform the headset smoke-test checklist below.
7. Only after the Development package passes, decide whether the client requires a
   Shipping/distribution-signed build or a directly installable review APK.

## Headset smoke test

- Application launches directly into VR without a black screen or immediate exit.
- Training mode selection opens `CNA_Map_01` correctly.
- Both controllers track and grip/release input works.
- Neck support, belt attachment, patient lift, and billboard carry work.
- The patient remains upright and faces the headset during belt carry.
- The wheelchair is recognized immediately inside its approach area.
- Final handle release inside the seat zone seats the patient once.
- `SittingIdle_1__UE` directly owns the final seated pose.
- UI text is readable and required audio/media assets load on device.
- Frame pacing, thermal behavior, and memory remain acceptable for the full flow.
- Relaunching the installed APK repeats the flow without requiring editor state.

## Build artifacts to retain

- APK and any generated install script
- Full packaging/AutomationTool log
- Git commit hash used for the build
- Build configuration and version identifiers
- Target headset model and OS version
- Smoke-test result and any known client-facing limitations
