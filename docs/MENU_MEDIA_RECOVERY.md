# Menu and media recovery

`IMC_UIInteract` is also registered in Enhanced Input startup defaults at priority 2, so OpenXR creates its action set before attaching the XR session. `bAddImmediately=False` keeps activation under the pawn component. Adding the context only at pawn BeginPlay was insufficient on Quest.

The VR pawn owns all trigger clicks through `VRWidgetInputComponent` and `IMC_UIInteract`. Both controller triggers use a 0.5 activation threshold. Each press is paired with a release on the component that received it. Training pointers retain left index 0 and right index 1, virtual user 0. The recovery menu has a dedicated `UMenuCursorInteraction` with index 2 and virtual user 1, so closing it cannot unregister the training pointers' Slate user.

The recovery menu dot initializes at the panel center without stick input. The hand that opens the menu owns its thumbstick movement and trigger clicks: a left-opened menu accepts only left input, and a right-opened menu accepts only right input. Cursor speed is 400 menu pixels/second. Its hit result explicitly targets the menu's widget plane, using the widget's actual desired render size. The menu Blueprint retains head-facing placement and animation; its hybrid controller-ray/cursor tick branch is disconnected. Closing the menu releases input immediately and blocks clicks during the closing animation. Other training widgets keep their controller rays.

Restart reloads the current level, clears playback, and releases held pointers. Reset Orientation recenters yaw while preserving floor height. Real life exits the application.

`TrainingMediaControllerComponent`, on `BP_MediaEventHandler`, owns the shared `MP_Display` player. Existing display StartPlayback and StopPlayback events forward requests and cancellation. Only NotifyPlaybackComplete broadcasts the existing OnPlaybackStopped training delegate. Failed or canceled playback never advances training.

Playback has a ten-second startup deadline and a five-second progress deadline, checked every half second. One automatic retry is permitted. Exhausting that retry shows a native Retry widget in the display's existing TextWidget component. Manual Retry starts a fresh attempt with a new automatic retry budget. Repeated overlap requests are ignored while an attempt is active, and completed displays cannot complete twice. Map restart resets all state.

The input directory is explicitly included in `DirectoriesToAlwaysCook`, because Unreal omitted the native default soft references in the first cook. Both movies retain relative `./Movies/` paths. Packaging stages Movies as NonUFS so AndroidMedia can read them through the APK's asset/OBB file handling.

## Verification

- Editor C++ build and affected Blueprint compilation: passed.
- Automation test `CNA.Reliability.Input.MenuCursorRouting`: passed. Uses the actual pawn/menu classes to verify immediate dot initialization, dedicated virtual user/pointer indices, menu-plane hit mode, movement, centered position, close cancellation, and restoring training pointers.
- Automation test `CNA.Reliability.Input.OpenXRStartupRegistration`: passed. Checks enabled startup defaults, registration priority, loadable context, and both Quest trigger bindings.
- Automation test `CNA.Reliability.Media.RecoveryPolicy`: passed. Covers startup deadline, stall deadline, automatic/manual retry, duplicate events, cancellation, and exactly-once completion.
- `Tools/verify_menu_media_reliability.py`: passed in a fresh commandlet. Checks trigger mappings, autoplay, both placed displays, shared player, and relative movie paths without saving the training map.
- Android ARM64 C++ build: passed.
- APK packaging and final runtime smoke check: passed; see the final verification record below.

## Development diagnostics

Log categories: `LogCNAWidgetInput`, `LogCNAMedia`, and `LogCNARecovery`.

- `cna.UI.SmokeCursorClicks`: opens the real menu, substitutes observers for button actions in that test instance, checks all three buttons for both opening hands, verifies opposite-hand clicks are blocked, checks close/reopen, then exits. Run with a renderer; this cannot be verified with NullRHI.
- `cna.Media.ForceOpenFailure 1`: force immediate opening failure. Restore with 0 before manual retry.
- `cna.Media.ForceStall 1`: ignore playback progress to exercise the stall deadline. Restore with 0 before manual retry.
- `cna.Media.SmokeOpenFailure`: in a running training map, exercise opening failure, automatic retry, the Retry button, duplicate requests, and error-widget cleanup; the process then exits with the smoke-test status.

Fault injection is excluded from Shipping builds.

## Headset acceptance record

Quest 3S was tested on 2026-10-05. After startup input registration and the dedicated-dot fix, physical left-trigger activation of all three recovery buttons was verified in live device logs, including repeated successful training-map reloads. Other headset models and media/full-training acceptance remain unverified.

| Model | Menu and restart | 20 bathroom attempts | Recovery and full training |
|---|---|---|---|
| Quest 2 | Not tested | Not tested | Not tested |
| Quest 3 | Not tested | Not tested | Not tested |
| Quest 3S | All three physical left-trigger actions verified; repeated successful map restarts | Not tested | Not tested |
| Quest Pro | Not tested | Not tested | Not tested |

For each model, test both controllers, quick/held clicks, hand switching, repeated menu opening, focus loss, and restarting during normal/failed playback. Repeated bathroom runs must include cold starts, restarts, overlap reentry, and bedroom/bathroom transitions. Verify natural completion once, Retry without Continue on failure, subsequent video/quiz/Continue behavior, and the patient-transfer workflow. Capture headset OS, build hash, and ADB logcat.

## Toolchain

Installed build tools: `D:/VSBuildTools`; Windows SDK: `D:/WindowsKits`; Visual Studio download cache/shared files: `D:/VSBuildCache` and `D:/VSShared`. Microsoft installers also keep mandatory installer metadata/system package caches in Windows-managed locations. New APK archives are placed under `D:/CNA_Builds/MenuMediaFix`. Existing Android SDK/JDK installations are reused.

Pre-migration Blueprint backups are under `Saved/Reliability/BeforeFix`. The already-modified `CNA_Map_01.umap` was not rewritten by the migration.

## Initial build verification record (2026-10-05)

The actual training map ran headlessly with the Development command `cna.Media.SmokeOpenFailure`. The bathroom display reached automatic retry after the first injected opening failure and manual recovery after the second. The test clicked the real native Retry button's bound delegate, verified a fresh retry budget and duplicate-request protection, canceled playback, and verified that the original display widget was restored. Exit status was 0, with `Runtime startup-failure, Retry-button, duplicate-request and cancellation smoke test: PASS` in `Saved/ReliabilityRuntimeSmoke.log`. There was no training completion dispatch.

This verifies runtime recovery integration without a headset; it does not verify physical controller hit testing, Android decoding, visual layout, or the complete patient-transfer flow. The decoder-independent automation test verifies the five-second stall deadline. Device fault injection and the full acceptance sequence above remain required.

`Tools/verify_reliability_apk.py` checks the two movies inside the APK's embedded OBB, including storage without compression and SHA-256 equality with the source films. It also compares the APK's containers with the staged container verified by UnrealPak and its native library with the final stripped runtime. Run it after generating `Saved/Reliability/AndroidContainer.csv` with UnrealPak's `-ListContainer` and `-Csv` options.


Initial Development APK: `D:/CNA_Builds/MenuMediaFix/Android_ASTC/CNABedToWheelchair-arm64.apk` (685,362,396 bytes).

APK SHA-256: `e857b84799c465f5e5f21d2a600cf6877ac8ac35309de55744219430175077de`.

The final archive contains `IA_UIInteract_Left`, `IA_UIInteract_Right`, and `IMC_UIInteract` in its verified IoStore container. Both required MP4s are stored without compression and match their original SHA-256 hashes. The APK's ARM64 library matches the final built runtime. Full package verification is in `Saved/Reliability/FinalApkVerification.json` and a copy alongside the APK. BuildCookRun completed with exit code 0.

Editor modules and the final Android runtime compiled successfully. The final editor build's training-map smoke test passed. ADB still reports no connected devices; all headset acceptance entries remain unverified.


## Quest click follow-up (2026-10-05)

The Quest 3S application log showed that `VRPawn` bound the new click handler in `TrainingModeSelectionMap`, but no trigger actions reached it. UE 5.5 OpenXR's `BuildActions` reads `UEnhancedInputDeveloperSettings.DefaultMappingContexts` before attaching the session. The previous fix added `IMC_UIInteract` only to the local player after pawn creation; that context was absent from the startup list. This prevented trigger clicks in both the patient's-house selector and the three-button menu.

`Config/DefaultInput.ini` now enables the startup list and registers `IMC_UIInteract` at priority 2. The pawn still activates and owns the mapping. Logs now identify startup registration, trigger action events, and focus suppression. The new registration regression test and existing media recovery test passed (2 successful, 0 failed). The rebuilt APK's actual `DefaultInput.ini` was extracted and checked to confirm the registration is packaged.

Corrected APK: `D:/CNA_Builds/Quest3S_UIFix/Android_ASTC/CNABedToWheelchair-arm64.apk`.

SHA-256: `0533f4aaa2e4c42f7b302b4604fd73ebbe1b4741acd3f4dce7abcec56323ebb7`.

APK signing, input assets, both source movies, staged containers, and the final native runtime passed verification. Device click results are recorded below when available.

## Cursor-menu follow-up (2026-10-05)

The user confirmed Patient's House clicks on Quest 3S after startup OpenXR registration was fixed. The three recovery-menu buttons remained unclickable. Their intended controls are left thumbstick movement of the dot and left trigger activation.

Device logging recorded trigger events with `interaction=None` while the menu was open. Routing previously depended entirely on the menu's cached `WidgetInteractionRefLeft/Right` variables. It now calls the menu's existing `GetWidgetInteractionComponent` for its selected hand, with a fallback to the corresponding pawn component. Destroyed menu references are ignored. Cursor positioning, menu toggles, and laser behavior remain in the existing Blueprint.

Editor automation: 3 passed, 0 failed, including real-asset cursor routing and destruction checks (`Saved/Reliability/MenuCursorTests/index.json`). A game-world inspection confirmed that the selected left pointer attaches to Cursor; the cursor has no collision. Device confirmation of all three buttons is pending the updated installation.

Development APK archive: `D:/CNA_Builds/Quest3S_CursorMenuFix/Android_ASTC/CNABedToWheelchair-arm64.apk`.

Cursor-menu package: Android build/cook/package passed in 2m55s; APK size 685,367,472 bytes; SHA-256 `aa07bd5261c887f6ea459cbafe21ee1e9dc4c3aa417ec1b29a05fb78ce6c3410`. APK v2 signature passed. Both movie hashes, cooked input assets, containers, and stripped native library were verified. Installed on Quest 3S with `adb install -r` (Success), preserving app data.

Quest 3S runtime probe: the actual packaged menu selected the left pointer, attached it to Cursor, and retained distinct indices 0/1 (`Saved/QuestCursorMenuProbe.log`). This verifies routing, not physical button activation. The app was then cold launched normally for user testing.

## Dedicated dot pointer follow-up (2026-10-05)

The user reported that the prior cursor-routing update still failed, and the dot appeared only after moving the right stick. Latest Quest 3S logging (`Saved/QuestMenuStillFails.log`) showed left trigger actions arriving with no hovered widget. Menu opening showed the pawn pointer still parented to a controller aim component. The previous tests validated reference routing but did not validate actual menu button clicks.

The hybrid Blueprint mode switched between the active controller ray and a stick cursor according to the other controller's hit on any world widget. It has been replaced by the dedicated dot behavior described above. The prior menu asset is backed up at `Saved/Reliability/BeforeDedicatedCursorMenu/Menu.uasset`; the training map is not saved or changed by this migration.

Editor build and menu Blueprint compilation passed. Automation: 3 passed, 0 failed (`Saved/Reliability/DedicatedCursorTests/index.json`). Rendered game-world test sends real pointer presses/releases to Reset Orientation, Restart, and Real life and observes each separate OnClicked event exactly once; it also closes/reopens the menu and verifies the dot is present and interactable. The observers replace actions only in that one-shot smoke instance, preventing map reload/quit from ending the test prematurely. This verifies click delivery; action behavior and physical stick/trigger operation still need headset testing.

Dedicated-dot Development APK: `D:/CNA_Builds/Quest3S_DotMenuFix/Android_ASTC/CNABedToWheelchair-arm64.apk`, 685,388,328 bytes, SHA-256 `a54281d278fe7c35ca62c9e92dfe82919967bd9a787c533e97125d377a29e672`. Build/cook/package passed in 4m1s. V2 signature, both movie byte hashes, cooked input assets, staged containers, and stripped native runtime hashes passed verification (`Saved/Reliability/DedicatedCursorApkVerification.json`). Final rendered click/reopen smoke passed after the immediate-reopen correction (`Saved/SlateMenuClicks.log`).

Quest 3S packaged runtime smoke passed (`Saved/QuestDedicatedDotSmoke.log`): all three real Slate button events arrived once, and the dot was visible/interactable after reopening. The test instance used observers instead of native actions and exited; the app was then cold launched normally.

Physical-controller test recorded in `Saved/QuestDedicatedDotLive.log`, application PID 17413:
- 15:37:57: left trigger selected Patient's House; training map loaded.
- 15:38:07 and 15:38:08: left trigger activated Reset Orientation.
- 15:38:11 and 15:38:18: left trigger activated Restart; each request reloaded CNA_Map_01 successfully and rebuilt menu/input state.
- 15:38:24: left trigger activated Real life; application exited.
- Each menu open logged dedicated dot initialization and interactable hit testing. These logs verify physical button activation and routing across restarts. User-visible immediate dot appearance has been requested for confirmation; the runtime test confirms dot visibility without stick input. No right-stick input is read by the new cursor implementation.

Quest 3S menu click actions: device-verified. Quest 2, Quest 3, and Quest Pro remain untested. Bathroom repetitions, forced media recovery on device, and the full patient-transfer workflow remain separate acceptance work.
