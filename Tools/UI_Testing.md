# Enabled training UI

After pulling this commit, regenerate project files if needed and build **CNABedToWheelchairEditor / Development / Win64** with Unreal Engine 5.5 before opening the project. This change includes native UI preview adapters, so an older editor DLL will not provide the new preview functions.

Open `/Game/Project/Maps/CNA_Map_01` and choose Play or VR Preview. The initial quiz is enabled. Patient conversation appears at its placed location only after the bathroom lesson and room transition. Menus, videos, the wrong-answer popup, Continue panel, and transfer hints use their existing training events.

On the placed **BP_PatientChat**, **Enable Patient Conversation** is checked and
**UI Testing -> Show Conversation for UI Testing** is unchecked. The native transfer
manager has **Guided Training** enabled and owns conversation timing and progression.
See `docs/GUIDED_TRAINING_STABILIZATION.md` for the current handoff and retry behavior.

Quizzes, videos and bed seating now allow turning and movement within a two-metre
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

For the lift/carry phase, use **Carry Popup Offset** separately. It follows the
moving pelvis: X moves away from the viewer, Y to the viewer's right, and Z up,
in centimetres. The default (0, 85, 65) keeps it beside the patient. Other phases
continue to use **Transfer Instruction Offset**. Empty video captions stay hidden.

The current editor version uses a 2 m lesson movement radius, darker cyan hints,
outlines on floating guidance only, and 5 px more popup padding. Button and popup
text has no outline. The chair hint chooses the nearest
ready receiving chair before approach, then follows the native latched chair.

## Lesson movement and floor hologram

In **CNA_Map_01**, select **TransferManagerActor > CNA > Training > Movement**.
Set **Lesson Movement Radius** in metres and toggle **Show Allowed Walk Hologram**.
The default is 2 m with the translucent cyan floor circle enabled. Both settings
update live in PIE; save your chosen values on the placed actor after stopping PIE.
They are baked into the packaged map and have no in-game configuration UI.

The circle uses the quiz panel or video screen as its horizontal centre, with its
surface positioned just above the floor. Hints does not control it. Turning and
tracked head/hand movement remain available. Thumbstick movement and teleport
destinations stay within the circle during the visible quiz or playing video.
Wrong-answer, loading/retry, Continue, conversation and transfer popups have no
radius restriction and hide the circle. Existing transition/menu/chair-seating
locks still apply. Returning from a popup to a quiz or entering another lesson
outside its circle uses the pawn fade to move to a verified walkable point inside it.
Navigation is used where available; otherwise floor collision and standing clearance
are checked. Resume from a loading/retry popup also checks the lesson boundary.
If no safe point fits, explicit feedback asks the developer to move the panel or
increase the radius; no unchecked position is used.

Each quiz/video arrival now starts on the **front circumference**, at the full
**Lesson Movement Radius**, facing the panel. The player can move toward the
panel, but backward controls cannot move them beyond the edge. Existing physical
head/hand tracking remains free. A 0.1 mm inset prevents rounding errors; it does
not create a meaningful backward walking allowance.

The preferred front point is checked against actual floor and standing clearance.
If furniture blocks it, another point on the front arc is used at the same radius.
If the entire front arc is blocked, explicit feedback asks for a radius/placement
change. Popup walking freedom and transition/menu/chair seating locks are retained.

For the patient quiz, the automatic direction favours the chair side of the bed.
An optional **Target Point** assigned to **Patient Quiz Arrival Target**, under
**CNA > Training > Movement**, chooses the arrival direction. Its direction is
projected onto the circumference; its distance and arrow rotation do not override
the full-radius placement or facing toward the quiz.

The final score/time panel uses five centred lines: **Transfer Complete!**,
**Score**, **Grade**, **Time taken** (whole seconds), and **Result**. It uses bold
teal type without outlines, adds **24 px** of bottom padding, and has a taller
render surface so long results remain fully readable. Adjust **Result Popup
Extra Bottom Padding** under **CNA > Training > Popups** to change that space.
The floating "Grab the front belt handle" text has its own **Belt Handle Label
Offset**, under the manager's **TransferGuidance** component > **CNA > Hints >
Visual Settings**. X moves toward the viewer (default 40 cm), Y to the viewer's
right, and Z upward. This moves the text while the grab marker stays at the belt.

Focused circumference validation: run py with the absolute path to Tools/verify_circumference_arrivals.py in a rendered editor game on CNA_Map_01. The development-only cna.Guided.TestArrivalStage command skips lessons solely for these placement checks. Expect four GUIDED_MOVEMENT RESULT: PASS records (stages 0, 2, 6, 7). Standing clearance uses the pawn's actual capsule and simple collision, with controller yaw synchronized after arrival.

Patient-video completion regression: run Tools/verify_single_quiz_fade.py using its absolute path in a rendered game on CNA_Map_01. It invokes normal media end handlers after playback progresses, checks the retained bathroom Continue/room transition, then walks during and after patient-video completion and repeats end callbacks. Require exactly one GUIDED_FADE: Out stage=7 and one GUIDED_FADE: In stage=7, plus the movement and SINGLE_FADE_TEST PASS records. Full clips are intentionally skipped in this focused regression.

All guided fades: run Tools/verify_transition_fades.py from TrainingModeSelectionMap (absolute script path). It uses the residence selection, quiz and Continue callbacks, samples the actual FadeMaterialInstance opacity, and moves the camera during initial and bathroom fade-in. Require one black/clear cycle and one GUIDED_FADE Out/In pair for stages 0, 1, 4, 6, 7. Tools/verify_media_recovery_fade.py checks a successful Retry after walking outside the circle: exactly one corrective pair at stage 2. Normal playback activation and boundary visibility no longer initiate fades; popup dismissal and media recovery explicitly request correction.
