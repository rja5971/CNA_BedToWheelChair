# Enabled training UI

After pulling this commit, regenerate project files if needed and build **CNABedToWheelchairEditor / Development / Win64** with Unreal Engine 5.5 before opening the project. This change includes native UI preview adapters, so an older editor DLL will not provide the new preview functions.

Open `/Game/Project/Maps/CNA_Map_01` and choose Play or VR Preview. The patient conversation is enabled and appears at its placed location near the patient. The initial quiz is enabled. Menus, videos, the wrong-answer popup, Continue panel, and transfer hints use their existing training events.

On the placed **BP_PatientChat**, both **Enable Patient Conversation** and **UI Testing → Show Conversation for UI Testing** are checked. Uncheck only the second option after testing to restore event-controlled conversation presentation. The conversation policy and button guards remain active.

For the normal entry sequence, launch `/Game/Project/Maps/TrainingModeSelectionMap` and select the patient's residence. Use the controller menu to test Reset Orientation, Restart, Exit, and Hints.

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

UI enabling does not repair the guided patient transfer. The second quiz answer and lift prompt still refer to the hidden legacy patient. The legacy `BP_Belt` actor is absent, so its actual guided belt interaction cannot be restored by a visibility switch. The belt preview deliberately does not create or animate a second patient/belt workflow. Record these issues while testing.

`WBP_CompletionPanel` is the intermediate Continue panel. Native transfer completion supplies score/instruction text; a separate final score widget has not been confirmed. `WBP_VRMenu` is an unused empty asset; the actual menu uses `WidgetMenu`.

To test the saved changes on **Meta Quest standalone**, package and install a fresh Android APK. The existing APK still contains the old disabled conversation configuration.

## Backups and verification

The original conversation Blueprint and training map are backed up under `Saved/UITesting/BeforeEnable/Project/`. The editor migration can be repeated with `-run=CNAReliability -EnableTrainingUI`; it does not rewrite other gameplay stages.

Local build and enable logs are under `Saved/UITesting/`. Runtime verification results are in `Saved/UITesting/Verification.json`. These ignored local artifacts are not included in the commit.

Validation completed: editor build passed; runtime checks passed for persisted conversation flags, startup visibility/input/owner, initial quiz, conversation response, both quiz previews, wrong-answer dismissal, lift/belt/menu previews, and preview cleanup. Both complete videos previously played on the Windows decoder (hand-rub approximately 52 seconds; transfer approximately 159 seconds), including their Continue/quiz completion callbacks. Quest Android playback, audible output, and the complete learner sequence still require headset testing.
