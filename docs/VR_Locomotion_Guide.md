# VR Locomotion Settings System — Complete Blueprint Guide

## ✅ Completed Steps
- Step 1: Enumerations created (E_LocomotionMode, E_TurnMode)
- Step 2: Game Instance Blueprint created (BP_VRGameInstance)
- Step 3 (partial): SaveSettings function done in BP_VRGameInstance

---

## Step 3 (Continued): LoadSettings Function

Open `BP_VRGameInstance` → My Blueprint panel → Functions → click **+** → name it `LoadSettings`

Double-click `LoadSettings` to enter the function graph.

### Build the nodes:

**1.** Drag off the entry node exec → search **"Does Save Game Exist"**
- Slot Name: `LocomotionSettings`

**2.** Drag off the **Return Value** (boolean) → **Branch**

**3.** From the **True** pin of Branch → search **"Load Game from Slot"**
- Slot Name: `LocomotionSettings`
- User Index: `0`

**4.** Drag off the **Return Value** of Load Game from Slot → **"Cast to BP_LocomotionSaveGame"**

**5.** Drag off **"As BP Locomotion Save Game"** pin → **Promote to Local Variable** (name it `LoadedSave`)

**6.** Now SET your Game Instance variables from LoadedSave. For each one:
- Drag `LoadedSave` (GET) into the graph
- Drag off it → **Get Locomotion Mode**
- Then separately: drag off the exec → **Set Locomotion Mode** (this sets it on `self`)
- Plug the GET result into the SET input

Do this for all 5 variables:

```
SET self.LocomotionMode     ← GET LoadedSave.LocomotionMode
SET self.TurnMode           ← GET LoadedSave.TurnMode
SET self.ContinuousMoveSpeed ← GET LoadedSave.ContinuousMoveSpeed
SET self.ContinuousTurnSpeed ← GET LoadedSave.ContinuousTurnSpeed
SET self.SnapTurnAngle      ← GET LoadedSave.SnapTurnAngle
```

**7.** The **False** pin of the Branch → leave unconnected (defaults are already set on the variables)

### Final exec chain:
```
Entry → Does Save Game Exist → Branch
  [True] → Load Game from Slot → Cast to BP_LocomotionSaveGame → SET LoadedSave
    → Set LocomotionMode → Set TurnMode → Set ContinuousMoveSpeed
    → Set ContinuousTurnSpeed → Set SnapTurnAngle
  [False] → (nothing)
```

---

## Step 3D: Call LoadSettings on Game Instance Init

1. In `BP_VRGameInstance`, go to the **Event Graph** tab (click the tab at the top)
2. Right-click → search **"Event Init"** → place it
3. Drag off the exec pin → **"Call Load Settings"** (or just search "LoadSettings" — it's your function)
4. That's it!

---

## Step 4: Modify VRPawn — Add Continuous Movement

Open `Content/VRTemplate/Blueprints/VRPawn` (double-click the asset)

### 4A: Add a variable to cache the Game Instance

1. In My Blueprint → Variables → click **+**
2. Name: `GameInstanceRef`
3. Type: click the type dropdown → search `BP_VRGameInstance` → select **Object Reference**

### 4B: Cache it on BeginPlay

1. In the **Event Graph**, find the **Event BeginPlay** node (it should already exist)
2. At the END of the existing BeginPlay chain (after whatever is already there), add:
   - **Get Game Instance** (right-click → search it)
   - Drag off its return → **Cast to BP_VRGameInstance**
   - Drag off "As BP VR Game Instance" → **SET GameInstanceRef** (your variable)

### 4C: Find the existing IA_Move logic

Look in the Event Graph for a node that says **EnhancedInputAction IA_Move**.
- It will have pins like **Triggered**, **Started**, **Completed**
- The **Triggered** pin likely connects to the teleport arc/trace logic

**IMPORTANT:** Don't delete the teleport logic. Instead:

1. **Disconnect** the wire from the **Triggered** exec pin (right-click the wire → Break Link)
2. We'll re-route through a Switch node

### 4D: Add the locomotion mode branch

1. Drag off the **Triggered** exec pin → search **"Get GameInstanceRef"** (your cached variable)
   - Actually: first pull the exec out, then separately get the variable
   
Better approach:
1. Drag `GameInstanceRef` (GET) from the My Blueprint panel into the graph
2. Drag off it → **"Get Locomotion Mode"**
3. Drag off LocomotionMode → search **"Switch on E Locomotion Mode"**
4. Connect the **Triggered** exec pin → into the Switch node's exec input

Now you have two output exec pins: **Teleport** and **Continuous**

### 4E: Reconnect Teleport

- From the **Teleport** pin of the Switch → connect to whatever the old teleport logic was connected to

### 4F: Build Continuous Movement (from the Continuous pin)

From the **Continuous** exec pin, build this chain:

**1. Get the input value:**
- Go back to the **EnhancedInputAction IA_Move** node
- Drag off the **Action Value** pin (it's a blue pin, type InputActionValue)
- Search **"Get Action Value (Axis2D)"** or **"Conv_InputActionValueToAxis2D"**
  - If you don't find that, drag off Action Value → **"Return Value as Axis2D"** or search **"Action Value Vector2D"**
- This gives you a **Vector2D**
- Drag off it → **"Break Vector 2D"** → gives you **X** (left/right) and **Y** (forward/back)

**2. Get Camera Forward (flattened to ground plane):**
- Right-click → search **"Get Camera"** or find the Camera component in VRPawn's components
- Better: search **"Get Actor Forward Vector"** — NO, we want the HMD/camera direction
- Best approach: 
  - In VRPawn's components, find the **Camera** component (it should be there)
  - Drag the Camera component from the Components panel into the graph
  - Drag off it → **"Get Forward Vector"**
  - Drag off result → **"Break Vector"** → gives X, Y, Z
  - **Make Vector** → X = Forward.X, Y = Forward.Y, Z = **0** (flatten it)
  - Drag off → **"Normalize"** → this is your `FlatForward`

**3. Get Camera Right (flattened):**
- Same Camera component → **"Get Right Vector"**
- Break Vector → Make Vector (X, Y, Z=0) → Normalize → `FlatRight`

**4. Calculate movement direction:**
- **FlatForward** × **Y** (from input) = use **"Multiply (Vector × Float)"** → `ForwardMove`
- **FlatRight** × **X** (from input) = **"Multiply (Vector × Float)"** → `RightMove`
- **ForwardMove + RightMove** = **"Add (Vector + Vector)"** → `MoveDirection`

**5. Apply speed and delta time:**
- GET `GameInstanceRef` → GET `ContinuousMoveSpeed`
- Right-click → search **"Get World Delta Seconds"**
- `MoveDirection` × `ContinuousMoveSpeed` × `DeltaSeconds` = use two Multiply nodes chained
  - First: MoveDirection × ContinuousMoveSpeed → result
  - Then: result × DeltaSeconds → `FinalMovement`

**6. Move the pawn:**
- Search **"Add Actor World Offset"**
  - Delta Location: plug in `FinalMovement`
  - Sweep: **check the box (True)**
  - Teleport: leave unchecked (False)

### 4G: Full chain from Continuous pin:
```
[Switch → Continuous pin]
  → (all the math nodes above are data connections, no exec needed for math)
  → Add Actor World Offset (this is the only exec node you need to connect)
```

Actually, since this is all pure math feeding into one function call, your exec chain is simply:
```
Switch (Continuous pin) → Add Actor World Offset
```
And all the math nodes (Get Forward, Break, Multiply, etc.) connect via their blue/green data wires into the Add Actor World Offset's Delta Location pin.

### 4H: Toggle Teleport Visualizer on BeginPlay

After the SET GameInstanceRef node on BeginPlay, add:

1. GET GameInstanceRef → GET LocomotionMode
2. **Switch on E_LocomotionMode**
3. **Teleport pin:** (do nothing, or explicitly set visualizer visible)
4. **Continuous pin:**
   - Find the Teleport Visualizer component (in the Components panel — it might be called `TeleportVisualizerComponent` or similar)
   - Drag it into the graph
   - → **"Set Visibility"** → uncheck New Visibility (= False)
   - → **"Set Component Tick Enabled"** → uncheck Enabled (= False)

---

## Step 5: Modify VRPawn — Turn Logic

### 5A: Add a helper variable

In VRPawn's variables, add:
- `bCanSnapTurn` — Type: **Boolean** — Default: **True** (check the box)

### 5B: Find IA_Turn node

Find the **EnhancedInputAction IA_Turn** node in the Event Graph.
It should have **Triggered** and **Completed** exec pins.

### 5C: Disconnect existing snap turn logic

Disconnect the wire from the **Triggered** pin (right-click wire → Break Link).
Keep the old snap turn nodes — we'll reconnect them.

### 5D: Add Turn Mode switch

From the **Triggered** pin:
1. GET `GameInstanceRef` → GET `TurnMode`
2. Drag off TurnMode → **"Switch on E Turn Mode"**
3. Connect Triggered exec → Switch node exec input

### 5E: Snap Turn branch (from the SnapTurn pin)

**1. Get the X value of the stick:**
- From IA_Turn node → drag off **Action Value** pin
- → Get as Axis2D (or 1D depending on how IA_Turn is configured)
- If Axis2D: Break Vector2D → use the **X** value
- If Axis1D: use the float directly

**2. Check threshold + can snap:**
- **Absolute Value** of X → **"Float >= Float"** → compare against `0.7`
- GET `bCanSnapTurn`
- **"AND Boolean"** → both conditions
- → **Branch**

**3. True branch (do the snap):**
- **SET bCanSnapTurn = False** (uncheck the box)
- Get the X value → **"Sign of Float"** (returns -1 or 1)
- GET GameInstanceRef → GET SnapTurnAngle
- Multiply: Sign × SnapTurnAngle = `YawAmount`
- **"Make Rotator"** → Roll = 0, Pitch = 0, Yaw = `YawAmount`
- **"Add Actor World Rotation"** → Delta Rotation = the Make Rotator result

**4. False branch:** leave unconnected

### 5F: Reset snap turn on stick release

Find the **Completed** pin on the same IA_Turn EnhancedInputAction node:
- Drag off → **SET bCanSnapTurn = True** (check the box)

### 5G: Continuous Turn branch (from the ContinuousTurn pin)

**1. Get X value of stick** (same as above — from the Action Value)

**2. Calculate rotation:**
- X × GET `GameInstanceRef.ContinuousTurnSpeed` → Multiply
- × **Get World Delta Seconds** → Multiply again → `YawThisFrame`

**3. Apply rotation:**
- **"Make Rotator"** → Roll = 0, Pitch = 0, Yaw = `YawThisFrame`
- **"Add Actor World Rotation"** → Delta Rotation = Make Rotator result

### Summary of IA_Turn wiring:
```
IA_Turn (Triggered) → Switch on E_TurnMode
  [SnapTurn]:
    → Get X → Abs → >= 0.7?
    → AND bCanSnapTurn
    → Branch
      [True] → Set bCanSnapTurn=False → Sign(X) × SnapTurnAngle → Make Rotator → Add Actor World Rotation
      [False] → (nothing)
  [ContinuousTurn]:
    → Get X × TurnSpeed × DeltaSeconds → Make Rotator → Add Actor World Rotation

IA_Turn (Completed) → Set bCanSnapTurn = True
```

---

## Step 6: Create the Settings Widget

### 6A: Create the Widget Blueprint

1. In Content Browser → `Content/VRTemplate/Blueprints/`
2. Right-click → **User Interface → Widget Blueprint**
3. Name: `WBP_LocomotionSettings`
4. Double-click to open

### 6B: Design the UI (Designer tab)

You should be in the **Designer** tab by default. Build this layout:

**1. Root:** The default Canvas Panel is fine. Or switch to a Vertical Box as root.

**2. Add a Vertical Box** (drag from Palette panel on the left → Layout → Vertical Box)
   - In Details (right panel): set the anchor to fill, add padding (20 on all sides)

**3. Inside the Vertical Box, add these (drag into the hierarchy on the left):**

#### Title
- **Text Block** — in Details, set Text to `Locomotion Settings`, Font Size: 24

#### Spacer
- **Spacer** — Size: 20

#### Movement Row
- **Horizontal Box**
  - Inside it:
    - **Text Block** — Text: `Movement:` (set minimum desired width to 150)
    - **Spacer** — Width: 20
    - **Button** — name it `BTN_Teleport` (click on it, set Name in Details panel at top)
      - Inside the button: **Text Block** — Text: `Teleport`
    - **Spacer** — Width: 10
    - **Button** — name: `BTN_Continuous`
      - Inside: **Text Block** — Text: `Continuous`

#### Spacer
- **Spacer** — Size: 15

#### Turn Row
- **Horizontal Box**
  - Inside:
    - **Text Block** — Text: `Turn:` (min width 150)
    - **Spacer** — Width: 20
    - **Button** — name: `BTN_SnapTurn`
      - Inside: **Text Block** — Text: `Snap Turn`
    - **Spacer** — Width: 10
    - **Button** — name: `BTN_ContinuousTurn`
      - Inside: **Text Block** — Text: `Continuous Turn`

#### Spacer
- **Spacer** — Size: 20

#### Move Speed Row
- **Horizontal Box** — name: `SpeedRow`
  - Inside:
    - **Text Block** — Text: `Move Speed:`
    - **Spacer** — Width: 10
    - **Slider** — name: `Slider_MoveSpeed`
      - In Details: Min Value = 0, Max Value = 1, Value = 0.5
      - Set size: width 200
    - **Spacer** — Width: 10
    - **Text Block** — name: `TXT_SpeedValue`, Text: `250`

#### Turn Speed Row
- **Horizontal Box** — name: `TurnSpeedRow`
  - Inside:
    - **Text Block** — Text: `Turn Speed:`
    - **Spacer** — Width: 10
    - **Slider** — name: `Slider_TurnSpeed`
      - Value = 0.5
      - Width 200
    - **Spacer** — Width: 10
    - **Text Block** — name: `TXT_TurnSpeedValue`, Text: `90`

#### Spacer
- **Spacer** — Size: 20

#### Save Button
- **Button** — name: `BTN_Save`
  - Inside: **Text Block** — Text: `Save & Close`

### 6C: Make buttons "Is Variable"

For each button and each named element you need to access in code:
- Click the button in the hierarchy
- In the Details panel → check **"Is Variable"** checkbox (this makes it accessible in the Graph)

Do this for: `BTN_Teleport`, `BTN_Continuous`, `BTN_SnapTurn`, `BTN_ContinuousTurn`, `Slider_MoveSpeed`, `Slider_TurnSpeed`, `TXT_SpeedValue`, `TXT_TurnSpeedValue`, `BTN_Save`, `SpeedRow`, `TurnSpeedRow`

### 6D: Widget Graph Logic

Switch to the **Graph** tab (button at top right of the widget editor).

#### Add a variable:
- `GameInstanceRef` — Type: `BP_VRGameInstance` (Object Reference)

#### Event Construct (auto-created, find it or add it):

Wire this:
```
Event Construct
  → Get Game Instance
  → Cast to BP_VRGameInstance
  → SET GameInstanceRef
  → Call function "UpdateButtonVisuals" (we'll create this next)
  → SET Slider_MoveSpeed Value = GET GameInstanceRef.ContinuousMoveSpeed ÷ 500.0
  → SET Slider_TurnSpeed Value = GET GameInstanceRef.ContinuousTurnSpeed ÷ 180.0
  → SET TXT_SpeedValue Text = Round(GameInstanceRef.ContinuousMoveSpeed) → ToString
  → SET TXT_TurnSpeedValue Text = Round(GameInstanceRef.ContinuousTurnSpeed) → ToString
```

For "Set Value" on a Slider: drag the slider variable → drag off → "Set Value"
For Text: drag text variable → "Set Text" → need to convert float to text (use "To Text (Float)" or Round → "To String" → "Make Literal Text")

#### Create function: UpdateButtonVisuals

1. Functions → + → name: `UpdateButtonVisuals`
2. Inside:

```
GET GameInstanceRef → GET LocomotionMode
  → "Equal (Enum)" compare with Teleport
  → Branch
    [True]:
      BTN_Teleport → Set Background Color → (1.0, 0.5, 0.0, 1.0)  [Orange]
      BTN_Continuous → Set Background Color → (0.3, 0.3, 0.3, 1.0) [Grey]
      SpeedRow → Set Visibility → Collapsed
    [False]:
      BTN_Teleport → Set Background Color → (0.3, 0.3, 0.3, 1.0)
      BTN_Continuous → Set Background Color → (1.0, 0.5, 0.0, 1.0)
      SpeedRow → Set Visibility → Visible

GET GameInstanceRef → GET TurnMode
  → Equal → compare with SnapTurn
  → Branch
    [True]:
      BTN_SnapTurn → Set Background Color → Orange
      BTN_ContinuousTurn → Set Background Color → Grey
      TurnSpeedRow → Set Visibility → Collapsed
    [False]:
      BTN_SnapTurn → Set Background Color → Grey
      BTN_ContinuousTurn → Set Background Color → Orange
      TurnSpeedRow → Set Visibility → Visible
```

For "Set Background Color": drag the button variable → drag off → search "Set Background Color" → plug in a "Make Linear Color" node or "Make Slate Color".

For "Set Visibility": drag the widget → "Set Visibility" → dropdown choose `Collapsed` or `Visible`

#### Button Click Events

Go back to the **Designer** tab. For each button:
- Click the button
- In Details panel → scroll to **Events** section
- Next to **On Clicked** → click the green **+** button

This jumps you to the Graph with a new event node.

**BTN_Teleport OnClicked:**
```
→ GET GameInstanceRef → SET LocomotionMode = Teleport (select from dropdown)
→ Call UpdateButtonVisuals
```

**BTN_Continuous OnClicked:**
```
→ GET GameInstanceRef → SET LocomotionMode = Continuous
→ Call UpdateButtonVisuals
```

**BTN_SnapTurn OnClicked:**
```
→ GET GameInstanceRef → SET TurnMode = SnapTurn
→ Call UpdateButtonVisuals
```

**BTN_ContinuousTurn OnClicked:**
```
→ GET GameInstanceRef → SET TurnMode = ContinuousTurn
→ Call UpdateButtonVisuals
```

**Slider_MoveSpeed OnValueChanged:**
(Add from Designer → select slider → Events → On Value Changed → click +)

```
→ "Value" output (float 0-1) × 500.0 (Multiply node)
→ SET GameInstanceRef.ContinuousMoveSpeed = result
→ Round result → To String → SET TXT_SpeedValue Text
```

**Slider_TurnSpeed OnValueChanged:**
```
→ Value × 180.0
→ SET GameInstanceRef.ContinuousTurnSpeed = result
→ Round result → To String → SET TXT_TurnSpeedValue Text
```

**BTN_Save OnClicked:**
```
→ GET GameInstanceRef → Call "SaveSettings" (the function we made earlier)
→ Get Player Pawn (index 0) → Cast to VRPawn → Call "RefreshLocomotionSettings" (we'll add this to VRPawn)
→ Remove from Parent (this closes the widget)
```

---

## Step 7: Hook Settings Into VR Menu + Add RefreshLocomotionSettings to VRPawn

### 7A: Add RefreshLocomotionSettings to VRPawn

Open VRPawn → My Blueprint → Custom Events section or just right-click in Event Graph:
1. Right-click → **"Add Custom Event"** → name it `RefreshLocomotionSettings`
2. Wire from that event:

```
RefreshLocomotionSettings
  → GET GameInstanceRef → GET LocomotionMode
  → Switch on E_LocomotionMode
    [Teleport]:
      → GET TeleportVisualizer component → Set Visibility = True
      → Set Component Tick Enabled = True
    [Continuous]:
      → GET TeleportVisualizer component → Set Visibility = False
      → Set Component Tick Enabled = False
```

### 7B: Add Settings Button to the VR Menu

Open `Content/VRTemplate/Blueprints/Menu.uasset` (this is a Widget Blueprint)

**In Designer tab:**
1. Find the existing layout (look at the widget hierarchy on the left)
2. Add a **Button** somewhere logical (e.g., below existing buttons)
   - Name: `BTN_OpenSettings`
   - Inside: Text Block → "Settings"
   - Check "Is Variable"

**In Graph tab:**

Go to Designer → click BTN_OpenSettings → Events → On Clicked → +

Wire:
```
BTN_OpenSettings OnClicked
  → Create Widget
      Class: WBP_LocomotionSettings
      Owning Player: Get Owning Player
  → Return Value → Add to Viewport
```

### Alternative (better for VR): Display on the 3D menu widget

If the VR template uses a 3D Widget Component (the menu floats in 3D space), then "Add to Viewport" might not be ideal. In that case:

1. Open the **WidgetMenu** Blueprint (the 3D actor, not the widget)
2. Add a second **Widget Component** 
3. Set its Widget Class to `WBP_LocomotionSettings`
4. Default visibility: Hidden
5. In the Menu widget's BTN_OpenSettings click:
   - Get parent WidgetMenu actor → Get the settings Widget Component → Set Visibility True

But for simplicity, **"Add to Viewport"** will work — the widget will appear as a 2D overlay. You can upgrade to 3D later.

---

## Step 8: Test & Verify

### 8A: Compile Everything
1. Open each Blueprint you modified:
   - `BP_VRGameInstance` → click **Compile** (top toolbar) → should show green checkmark
   - `BP_LocomotionSaveGame` → Compile
   - `VRPawn` → Compile
   - `WBP_LocomotionSettings` → Compile
   - `Menu` → Compile
2. **Save All** (Ctrl+Shift+S)

### 8B: Verify Project Settings
- Edit → Project Settings → Maps & Modes → Game Instance Class = `BP_VRGameInstance`

### 8C: Play in VR
1. Click the dropdown next to Play → **VR Preview**
2. Test defaults:
   - Left stick should teleport (existing behavior)
   - Right stick should snap turn
3. Open wrist menu → click "Settings"
4. Switch to Continuous movement → left stick should now smoothly move you
5. Switch to Continuous Turn → right stick should smoothly rotate
6. Adjust sliders
7. Click Save & Close
8. Stop play → Play again → settings should persist

### Common Issues & Fixes

| Problem | Likely Cause | Fix |
|---------|-------------|-----|
| "Accessed None" error on GameInstanceRef | BeginPlay didn't run or wrong order | Make sure Cast succeeds; check Game Instance Class in Project Settings |
| Movement goes through walls | Sweep not enabled | Check "Sweep = True" on Add Actor World Offset |
| Movement direction is wrong | Using Actor Forward instead of Camera Forward | Make sure you're using the Camera component's forward/right vectors |
| Snap turn fires many times | bCanSnapTurn not resetting | Check that IA_Turn Completed event sets bCanSnapTurn = True |
| Slider shows wrong value | Division/multiplication mismatch | Ensure: slider 0-1 × 500 = speed; speed ÷ 500 = slider value |
| Widget doesn't appear | Owning Player is None | Use "Get Owning Player" or "Get Player Controller 0" |
| Teleport still works in Continuous mode | Switch node not connected properly | Double check the Switch on E_LocomotionMode is between Triggered pin and teleport logic |
| Can't find IA_Move node | Different event graph or collapsed nodes | Try searching (Ctrl+F in the Blueprint graph) for "IA_Move" |

---

## Quick Reference: All Assets Created/Modified

| Asset | Action |
|-------|--------|
| `E_LocomotionMode` | Created (Enumeration) |
| `E_TurnMode` | Created (Enumeration) |
| `BP_VRGameInstance` | Created (Game Instance BP) |
| `BP_LocomotionSaveGame` | Created (SaveGame BP) |
| `WBP_LocomotionSettings` | Created (Widget BP) |
| `VRPawn` | Modified (movement + turn branching) |
| `Menu` | Modified (added Settings button) |
| Project Settings | Modified (Game Instance Class) |

---

## Optional Enhancements (Later)

- **Vignette effect** during continuous movement (reduces motion sickness)
- **Speed ramping** — start slow, speed up as stick is held
- **Comfort options** — reduce FOV during movement
- **Haptic feedback** on snap turn
- **Reset to Defaults** button in settings widget
