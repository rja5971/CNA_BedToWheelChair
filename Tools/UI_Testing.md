# Enabled training UI

After pulling this commit, regenerate project files if needed and build **CNABedToWheelchairEditor / Development / Win64** with Unreal Engine 5.5 before opening the project. This change includes native UI preview adapters, so an older editor DLL will not provide the new preview functions.

Open `/Game/Project/Maps/CNA_Map_01` and choose Play or VR Preview. The initial quiz is enabled. Patient conversation appears at its placed location only after the bathroom lesson and room transition. Menus, videos, the wrong-answer popup, Continue panel, and transfer hints use their existing training events.

On the placed **BP_PatientChat**, **Enable Patient Conversation** is checked and
**UI Testing -> Show Conversation for UI Testing** is unchecked. The native transfer
manager has **Guided Training** enabled and owns conversation timing and progression.
See `docs/GUIDED_TRAINING_STABILIZATION.md` for the current handoff and retry behavior.

Quizzes, videos and bed seating now allow turning and movement within a five-metre
horizontal radius. Select **TransferManagerActor** in CNA_Map_01 and edit **CNA >
Training > Movement > Training Movement Radius Meters** to tune it. The chair hint
matches the final seated pose, and hand holograms have been removed.

For the normal entry sequence, launch `/Game/Project/Maps/TrainingModeSelectionMap` and select the patient's residence. Use the controller menu to test Reset Orientation, Restart, Quit, and Hints.

## Inspect a particular screen in the editor

Start Play/VR Preview in `CNA_Map_01`. Open **Tools → Debug → Output Log**, select **Python** in the command input, and run this once:

```python
import sys, unreal; sys.path.insert(0, unreal.Paths.project_dir() + 'Tools'); import preview_training_ui as ui
```

Then enter one command at a time:

| Command | What you can test |
|---|---|
| `ui.show('chat')` | Existing conversation panel brought in front of the camera; actual conversation buttons |
| `ui.show('quiz0')` | First question, including correct/incorrect answers |
| `ui.show('quiz1')` | Second question, including its existing patient handoff |
| `ui.show('wrong')` | Existing wrong-answer popup and OK dismissal |
| `ui.show('lift')` | Existing lift prompt and its legacy button callback |
| `ui.show('belt')` | Temporary belt widget for appearance and clicks; its transfer delegate is unbound |
| `ui.show('menu')` | Toggle the actual controller menu |
| `ui.show('handwash')` | Play the existing hand-rub video on the basin display; Continue appears on completion |
| `ui.show('transfer')` | Play the existing transfer video on the bedroom display; quiz appears on completion |
| `ui.clear()` | Remove temporary belt preview actors |

Preview commands change only the running session. Stop and restart Play before testing the complete training sequence. They are editor Python tools, not Quest standalone controls. Video previews retain their room display positions. The Retry UI appears when media opening/playback fails.

## Known functional limits

The guided quiz now starts the native patient transfer. Legacy lift/belt/chair
animation callbacks are inactive. Runtime preview commands cannot bypass guided
prerequisites and may be hidden again by the manager; use the normal journey to
test interactive screens at their actual stages.

`WBP_CompletionPanel` is the intermediate Continue panel. Native transfer completion now displays score/instruction text in the required status panel. `WBP_VRMenu` is an unused empty asset; the actual menu uses `WidgetMenu`.

To test the saved changes on **Meta Quest standalone**, package and install a fresh Android APK. The existing APK still contains the old disabled conversation configuration.

## Backups and verification

The original conversation Blueprint and training map are backed up under `Saved/UITesting/BeforeEnable/Project/`. The editor migration can be repeated with `-run=CNAReliability -EnableTrainingUI`; it does not rewrite other gameplay stages.

Local build and enable logs are under `Saved/UITesting/`. Runtime verification results are in `Saved/UITesting/Verification.json`. These ignored local artifacts are not included in the commit.

Validation completed: editor build passed; runtime checks passed for persisted conversation flags, startup visibility/input/owner, initial quiz, conversation response, both quiz previews, wrong-answer dismissal, lift/belt/menu previews, and preview cleanup. Both complete videos previously played on the Windows decoder (hand-rub approximately 52 seconds; transfer approximately 159 seconds), including their Continue/quiz completion callbacks. Quest Android playback, audible output, and the complete learner sequence still require headset testing.

## Position and read popup messages

In CNA_Map_01 select **TransferManagerActor** and expand **CNA > Training > Popups**.
Edit **Transfer Instruction Offset**, **Wrong Answer Popup Offset**, **Continue
Popup Offset**, or **Video Status Popup Offset**. Offsets are in centimetres;
transfer instructions use world axes relative to the placed patient actor, while
the other offsets use the widget's local axes from its original placement.
**Transfer Instruction Scale** changes the physical size of the instruction panel.

These values update while Play/VR Preview runs. After tuning in Play mode, copy
the chosen values into the placed level actor after stopping Play and save the
map before packaging. Popup backgrounds fit their render surfaces; messages wrap
inside the background with padding. Existing OK and Continue callbacks are retained.
