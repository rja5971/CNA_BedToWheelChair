# Natural patient grabbing and bed sit-up

## Runtime ownership

The existing Manny Simple mesh and skeleton are retained. `/Game/Patient/PA_CNA_Patient`
is a dedicated copy of the mannequin physics asset. Its 22 bodies form a 21-joint
tree; the old calf-to-pelvis loops are removed. Linear joint travel is locked.
Shoulders and hips have limited rotation, elbows and knees use hinge-like twist
limits, and spine/neck motion is restricted. Fingers are not individually grabbed.

`PatientBedSupportComponent` is the owner of bed-stage physics. The active map
explicitly references `SM_Bed`, defines its local mattress volume, and marks the
chair-facing edge. A simple box replaces furniture collision for the patient;
other tools retain the original furniture collision. Initial penetration is
corrected once at initialization. No pelvis or leg world anchors are applied.

The initial pose uses relaxed arms beside the torso when no rest animation is
assigned. The native pose proxy receives that pose before physics bodies are
recreated, with mass/damping reapplied. Mattress collision bears the untouched
patient's weight; upward cooperation begins only after torso/hip support. A finite
supine balance torque prevents idle hip/torso rolling and stops as soon as the
nurse begins torso/hip guidance. Joint frames coincide in the reference skeleton;
elbow/knee hinges rotate about anatomical flexion axes, rather than the inherited
mannequin frames. Physics substeps are capped at 1/120 second with up to eight
steps so low frame rates do not change mattress/contact handling.

The existing left/right pawn components are reused by `VRPatientCareBridgeComponent`.
`GrabComponent` chooses the belt handle first within its valid reach; otherwise it
chooses a nearby simulated body. Patient grabs retain the acquisition contact offset,
use position-only constraints, cap force, and limit target speed to 150 cm/s and
target lead to 12 cm. Unreachable targets produce lag rather than a forced pose.
Mattress targets account for the held body's complete collision bounds, including
a shin extending over the bed edge; checking only the hand/contact point is
insufficient. The resulting target still respects the 12 cm lead limit.
Grip acquisition retries every 0.08 seconds while held. Focus/tracking loss explicitly
cancels ownership and requires a new press. Belt attachment retires an obsolete
loose-belt constraint while retaining the held latch for acquisition of the handle.

Only a grabbed region relaxes. Both hands contribute to the region set; recovery
takes 0.35 seconds after the final hand releases a region. Local muscle targets
follow the touched pose, with weak limb tone and finite forces. Mass is unchanged.
Limb grip force is sized for the connected regional mass (1,200 engine force
units per kg, capped between 6,000 and 22,000), so gripping a small foot can guide
the connected leg. Head/neck and torso force limits retain their separate tuning.
Torso/hip support permits horizontal movement and pivoting; limb grabs do not
activate whole-body lifting assistance. Cooperation follows the supported posture
and does not automatically move the patient to the edge.

## Seating, lesson and transfer

Bed seating requires the existing torso-angle threshold, supported pelvis near
the edge, both feet outside and below the mattress top, and pelvis speed at most
15 cm/s for 0.4 seconds. Bed posture is separate from belt task state. Simulation
and active grips stay enabled after seating, with weak torso balance. Attaching a
belt retains bed support until the first carry-handle grab.

The quiz's old `Play Player Sitting` call is removed. The legacy
`BP_PatientInteraction` seating events, pose-transition functions and follow tick
are disconnected; its old patient and belt are hidden and collisionless. The
active native manager references `PatientActor_2`, `BeltActor_2` and
`WheelchairActor_2`. Existing media/menu reliability changes are preserved.
Bed seating no longer triggers the patient fade/reposition cinematic.

At carry start, body handles release and the bed component suspends once. Existing
animation-owned belt carrying, headset-facing behavior, chair selection and final
`SittingIdle_1__UE` seating remain the transfer owners. A carry release over the
bed resumes physical support. Spine stress uses parent-relative joint rotation so
whole-body turning does not count as spine bending.

## Assets and reproducibility

Binary backups from immediately before this change are in `Saved/PatientCare/BeforeFix`.
The migration is implemented by the editor commandlet `-run=PatientCareSetup -Apply`;
without `-Apply`, it writes read-only actor and Blueprint graph inspection reports.
Backups are not overwritten on repeated application.
`-RepairBedPose` reapplies only the dedicated physics asset's anatomical frames
and limits, backed up in `Saved/PatientCare/BeforeLinkPoseFix`. Constraint templates
must update their separate default profile before saving or Unreal serializes the
original limits. The asset test now checks fresh-loaded neutral-frame agreement
and knee hinge axes/locked swing. Scripted foot trajectories start on the foot's
physical center, rather than an ankle bone point inside the shin collider.

Editor automation tests are under `CNA.PatientCare`:

- `AssetsAndQuizIsolation`: dedicated body/joint structure and actual quiz graph.
- `ArticulatedGrabs`: contact acquisition, limb movement, two hands, regional recovery,
  unchanged mass, held retry, focus cancellation and carry handoff at 30/60/72 FPS.
- `EdgeSitUpTrajectories`: 20 fresh bed attempts at 30/60/72 FPS, torso/hip guidance,
  separate leg trajectories, released seated balance, belt attachment and actual carry.
- `HeldBeltAttachment`: loose-belt attachment while gripping, migration to the attached
  handle, and protection against arming a false final-release seating request.
- `UntouchedBedPose`: 45 seconds without grabs at 30/60/72 FPS, checking mattress
  clearance, a flat torso, resting head, low motion, and bounded hip/torso rotation.

Reports are exported to `Saved/PatientCare/Tests`. These are simulated hand paths,
not a substitute for controller comfort, anatomical visual inspection or full
physical headset acceptance. Device packaging/validation results are recorded below
after completion. Recording_3/4 scenario names refer to the supplied descriptions;
the recordings themselves were not supplied in this workspace.

The Development-only command `cna.Patient.SmokeBed20` runs on the actual training
map, using temporary synthetic hands and the placed patient, bed, belt, manager
and chair. It resets the bed pose only between test attempts, completes 20 guided
sit-ups with released balance, then checks belt acquisition, body-handle release,
carry ownership, chair selection and final animation/pelvis alignment. It exits
the test instance with a PASS/FAIL log. It is never installed during normal play
and its console entry is excluded from Shipping builds.

`cna.Patient.ViewBed` creates a desktop inspection camera beside the actual bed
only when explicitly invoked. Normal play and VR Preview retain their normal camera.

## Desktop validation

The Editor target builds successfully. Affected Blueprints compiled during asset
migration, including the existing pawn/media graphs. The full-map scripted runtime
probe passed 20 consecutive attempts plus the final belt-to-chair transfer at a
fixed 72 FPS (`Saved/PatientCare/RuntimeProbe.log`). The final automation run passed
all seven tests: the four patient tests and the existing menu routing, OpenXR action
registration and media recovery tests. Its 20 bed attempts covered 30/60/72 FPS and
the selected-chair final release. Android/device results are recorded below.

## Android delivery and headset status

The following records the earlier APK delivery. The subsequent menu-hand and
initial bed-pose corrections are being tested in the Editor for Quest Link;
Android packaging is deferred until the user finishes Link testing. The APK
below does not contain those subsequent corrections.

The earlier Android ARM64 Development/ASTC build, cook, stage, package and archive completed
successfully. The delivered APK is:

`Builds/Quest/NaturalPatientCare/Android_ASTC/CNABedToWheelchair-arm64.apk`

- Size: 685,341,184 bytes.
- SHA-256: `be7810ad22a0678d5d13162123d70467962b311eedf24baa3923cf0eed0ccf64`.
- APK v2 signature verification passed.
- The packaged native library matches the final stripped build.
- Embedded containers match the staged containers; the dedicated physics asset,
  bone mapping, patient/quiz Blueprints and training map are present.
- Both training movies retain their original bytes and remain stored/seekable.
- Package verification: `Saved/PatientCare/ApkVerification.json` and
  `Saved/PatientCare/ApkSignature.txt`, with copies beside the APK.

A Quest 3S was detected during preparation, but disconnected before installation.
ADB then reported no devices. No new APK was installed, and no packaged on-device
patient probe was completed. The user selected APK delivery and headset testing
later. Desktop scripted results must not be presented as physical Quest validation.

Once a Quest is connected, `Tools/validate_patient_quest.ps1` installs the APK with
`adb install -r` (preserving app data), runs the Development probe with synthetic
hands and NullRHI, captures `Saved/PatientCare/QuestRuntimeProbe.log`, and returns
to the normal application after success. `-Rendered` enables drawing for the
scripted run. Neither mode validates physical controller comfort or actual lesson
playback/answer timing.

For physical headset acceptance, repeat body grabs, two-hand support, release and
re-grab before playback, during playback, after completion and after answering,
including grips held across those transitions. Check natural elbow/knee bending,
joint-limit resistance, mattress clearance and belt-handle retention. Complete
20 consecutive torso/hip-to-edge and separate-leg sit-ups, then verify belt
attachment, carry and release into the selected wheelchair. Record these results
separately from the existing automated reports.
