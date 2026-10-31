# Keyboard and mouse for games

AniketosHEN lets a USB keyboard and mouse play any PS5 game. The game still
thinks it is reading a DualSense: every key, button, wheel notch and mouse
movement is turned into controller input, the way a XIM or Cronus adapter
does it, but inside the console and with no extra hardware.

The mapping is edited in the HEN Toolbox. Nothing is preset per game: every
game starts from one generic template, and you change it to suit the game.

# Part 1: User guide

## What you need

- A USB keyboard and a USB mouse (wired, or a wireless receiver), plugged
  into the console.
- A DualSense that is switched on and signed in as the player. The mapping is
  added to that controller's input; the controller itself keeps working.
- Set *Settings → Accessories → Controllers → Time Until Controllers Turn
  Off* to *Don't Turn Off*, or keep the DualSense on its USB cable. The
  system does not see the keyboard and mouse as controller activity, so it
  could otherwise switch the DualSense off in the middle of a game.

## Turning it on

1. Open the HEN Toolbox and go to *Keyboard & mouse*.
2. Turn on *Use keyboard & mouse in games*. It is off by default.
3. Start the game. If a game is already running, it gets the keyboard and
   mouse within a moment; there is no need to restart it.

The switch applies to every game. A game that has no profile of its own uses
the default template.

## Picking a game and editing its bindings

1. In *Keyboard & mouse*, open *Games*. It lists the installed games.
2. Choose a game. If it has no profile yet, choose *Create from default*:
   the game gets its own copy of the generic template.
3. Open *Bindings*, choose a layer, then choose a binding to edit it, or
   *Add binding* to make a new one.
4. In the binding editor, choose *Input* and press the key or mouse button
   you want (or roll the wheel). Then set the gesture, the mode and the
   outputs (what the controller does).
5. Changes are saved at once. If the game is running, the new profile reaches
   it within a moment; there is no need to restart the game.

*Reset to default* replaces the game's profile with a fresh copy of the
template. *Delete* removes it, so the game falls back to the default
template.

The default template is a plain third-person layout. Change any of it to
match the game's own controls screen.

| Input | Output |
|---|---|
| W A S D | left stick up, left, down, right |
| Mouse movement | right stick (Velocity mode) |
| Left mouse button | R2 |
| Right mouse button | L2, and holds the "aim" layer (same keys, mouse at half sensitivity) |
| Space | Cross |
| Left Shift | L3 |
| E, R | Square |
| F | Triangle |
| Q | L1 |
| 1 2 3 4 | D-pad up, right, down, left |
| Escape | Options |
| Tab | Touchpad |
| Pause | suspend key (see below) |

## Concepts

Each part below gives one example. All examples are made in the binding
editor.

### Inputs and outputs

An **input** is one thing you press: a key, a mouse button (left, right,
middle, back and forward side buttons, up to 8 buttons), a wheel notch (up,
down, tilt left, tilt right), or a mouse movement direction (moving the mouse
right faster than a set speed counts as "held").

An **output** is what the controller does. One binding can have up to four
outputs at once:

| Output | Does | Example |
|---|---|---|
| Button | presses one or more controller buttons | Space → Cross |
| Stick direction | pushes a stick axis to a value | W → left stick up |
| Trigger | pulls L2 or R2 to a value | Mouse left → R2 full |
| Stick scale | scales a stick while held | Left Alt → left stick at 40 % (walk) |
| Layer (hold) | turns a layer on while held | Left Ctrl → "Precise" layer |
| Layer (toggle) | turns a layer on or off | F → "Vehicle" layer |
| Macro | plays a macro | Q → "Quick heal" macro |
| Nothing | blocks the key in this layer | Left Shift → nothing, in "Vehicle" |
| Suspend mapping | pauses or resumes the whole mapping | another key than Pause |

A trigger pulled past a small threshold (30 of 255 by default) also sets its
L2 or R2 button bit, because some games read the button and others the
analog value.

### Layers (on foot, vehicle)

A layer is a set of bindings that sits on top of the layers below it. The
first layer (the base) is always on. Up to 8 layers.

- A key with a binding in the highest active layer uses that binding.
- A key with no binding there **falls through** to the next active layer
  below.
- A binding with the output *Nothing* stops the fall-through: the key does
  nothing while that layer is on.
- Each layer can have its own mouse settings, or use those of the layer below.
- A key keeps the binding it had when it was pressed until it is released,
  so changing layers while keys are held never leaves a button stuck.

**Example: on foot and in a vehicle.**

- Base layer "On foot": W/S/A/D on the left stick, mouse on the right stick
  (Velocity mode), Left Shift → Cross (sprint), F → Triangle (enter
  vehicle).
- Layer "Vehicle": W → R2, S → L2, Space → R1 (handbrake), Left Shift →
  *Nothing*, mouse settings slower and smoother.
- On F, add a second output: *Layer toggle: Vehicle*. Pressing F now presses
  Triangle **and** switches the layer, so getting in and out of a car swaps
  the controls. A/D have no binding in "Vehicle", so they still steer through
  the base layer.

The HEN cannot see what happens in the game. If the game refuses to let you
into the car, the layer is now wrong. Bind a spare key (for example Caps
Lock) to *Layer toggle: Vehicle* alone to fix it with one press.

### Gestures (when a binding fires)

| Gesture | Fires | Default time |
|---|---|---|
| Press | while the input is held | — |
| Tap | after a short press and release, as a short pulse | released within 200 ms |
| Long press | once the input is held long enough, until released | held 400 ms |
| Double tap | on the second press, while it is held | second press within 250 ms |
| Release | when the input is released, as a short pulse | — |

One key can carry several gestures. **Example:** Escape *Tap* → Options
(pause menu), Escape *Long press* → Touchpad (map). A quick press opens the
pause menu; holding it opens the map.

When a key has both a tap and a double tap, the tap waits until the double
tap time has passed, so it fires up to 250 ms late. Use double tap only
where that delay does not matter.

### Modes (what firing does)

| Mode | Outputs | Example |
|---|---|---|
| Hold | on while the binding fires | Mouse left → R2 (fire while held) |
| Toggle | each firing flips them on or off | Left Ctrl → L3 (crouch stays on until pressed again) |
| Turbo | pulse on and off while firing, at a rate (1–30 per second) and a duty (10–90 % on) | Left Shift → Cross at 8 per second (sprint by repeated taps) |
| Pulse | one short pulse per firing, however long the key is held | E → Triangle (interact once, even if E is held) |

Pulses last 60 ms by default. A game reads the controller once per frame,
so a pulse shorter than two frames (about 67 ms at 30 fps) can be missed.
Raise it if a tapped key sometimes does nothing.

### Chords

A binding can require up to two extra keys held at the same time (the
*modifiers*). A binding with more modifiers wins over one with fewer on the
same key.

**Example:** 1 → D-pad up (item 1), and Shift + 1 → D-pad up **and** Cross
(use item 1). Pressing 1 alone gives the first; holding Shift and pressing 1
gives only the second.

For many shifted keys at once, a layer held by the modifier key is simpler
than many chords.

### SOCD (opposite keys at once)

When A and D (or W and S) are both held, each layer decides what the stick
does:

| Setting | A then D held | Use |
|---|---|---|
| Last wins (default) | right (the newer key) | shooters: quick strafe changes |
| Neutral | centre (both cancel) | fighting games, fairness rules |
| First wins | left (the older key) | rarely needed |

**Example:** in a shooter, with *Last wins*, holding A and tapping D strafes
right during the tap and back left after it, without lifting A.

### Analog ramps and curves (racing)

A key is on or off, but a trigger or stick can be pulled part-way. A ramp
gives a key a gradual pull:

- **Rise** is the time from 0 to the full value while the key is held. 0 is
  instant.
- **Fall** is the time back to 0 after release. 0 is instant.
- **Curve** shapes the rise: 1.0 is linear, above 1.0 starts slowly
  (progressive), below 1.0 starts quickly. Range 0.1 to 10.
- **Value** is how far the output goes: a trigger at 50 % pulls half-way.

**Example: a racing game, in a "Driving" layer.**

| Input | Output | Value | Rise | Fall | Curve | Effect |
|---|---|---|---|---|---|---|
| W | Trigger R2 (throttle) | 100 % | 300 ms | 100 ms | 2.0 | throttle squeezes in over 0.3 s, slowly at first, so the tyres do not spin |
| S | Trigger L2 (brake) | 100 % | 150 ms | 0 | 1.0 | firm brake, released at once |
| A | Left stick X, left | 100 % | 200 ms | 120 ms | 1.0 | steering turns in smoothly and recentres smoothly |
| D | Left stick X, right | 100 % | 200 ms | 120 ms | 1.0 | the same to the right |
| Left Shift | Trigger R2 | 50 % | 0 | 0 | 1.0 | half throttle for tight corners |

Short taps on A and D now give small steering corrections instead of full
lock. Set the layer's SOCD to *Last wins* so a quick A-to-D switch steers
straight across.

**Steering with the mouse instead.** Set the "Driving" layer's mouse to:

- *Mode:* Position;
- *Stick:* left;
- *Range:* the mouse travel for full lock, in mouse counts, for example 800
  (about 1.3 cm at 1600 CPI). Default 400, range 50 to 20000;
- *Spring:* how fast the wheel returns to centre, in full lock per second,
  for example 0.5 (from full lock to centre in 2 s). 0 holds the last
  position. Default 3.0;
- *Y:X ratio:* its lowest, 0.1, so forward and back travel barely moves the
  stick;
- *Deadzone:* the game's steering deadzone, as for aim (below).

In Position mode the mouse moves the stick like a wheel: with a range of
800, moving it left 400 counts gives half lock to the left, which the spring
then brings back to centre unless you hold it with the mouse. Sensitivity
and boost do not apply; the range alone sets how fast it steers. Keep W/S on
the triggers.

### Mouse in Velocity mode (aim and camera)

In Velocity mode, mouse **speed** sets how far the stick is pushed, and
the game turns the camera at a rate set by that push. This is how XIM-style
aim works. Settings, per layer:

| Setting | Does | Start with |
|---|---|---|
| Stick | which stick the mouse drives | right |
| Sensitivity | how much stick a given mouse speed gives (0.01–50) | 1.0, then adjust |
| Y:X ratio | vertical speed relative to horizontal (0.1–5) | 1.0 (0.8 if up/down feels too fast) |
| Curve (exponent) | 1.0 is linear; below 1.0 boosts small moves, above 1.0 makes small moves finer (0.25–4) | 1.0 |
| Deadzone | the game's own inner deadzone, skipped so the smallest move already turns (0–0.5) | 0.20, then measure |
| Deadzone shape | circular or square, as the game's deadzone is | circular |
| Outer | stick push at which the game's turn is already full (0.5–1) | 1.0 |
| Smoothing | the time window the speed is averaged over (4–100 ms) | 12 ms; 20–25 ms for a 125 Hz mouse or a vehicle camera |
| Boost | extra gain on fast flicks (1–4); 1.0 is none | 1.0, try 1.3 |
| Invert | flips X or Y | off |

With *Boost* above 1.0, a flick faster than the stick can follow is not lost:
the rest of the turn is paid out right after the hand slows down (up to a
short limit), so a quick 180 lands closer to where it was aimed. Moving the
mouse back the other way cancels it.

**Finding a game's deadzone.**

1. Set *Curve* 1.0, *Boost* 1.0, *Sensitivity* low and *Deadzone* 0.
2. In the game, move the mouse very slowly. If the camera does not turn at
   all until you move faster, the game has a deadzone.
3. Raise *Deadzone* in steps of 0.02 and repeat. Stop at the first value
   where the slowest movement already turns the camera smoothly.
4. If the camera now drifts or jumps at the start of every movement, go back
   one step.
5. Try a slow diagonal movement. If diagonals snap to straight up, down, left
   or right, the game's deadzone is square: change *Deadzone shape* to
   *Square*. If diagonals feel faster than straight moves with *Square*, use
   *Circular*.

Most games are between 0.10 and 0.25, circular.

**Then sensitivity.** Turn it up until a fast swipe just reaches the game's
fastest turn. Above that the stick is already full, so faster movements are
lost. If you turn too slowly at full speed, raise the game's own sensitivity
(see Tips), not this one.

**Then smoothing.** If the camera stutters during slow, steady movements,
raise *Smoothing*. If it feels late or floaty, lower it. Each 2 ms of window
adds about 1 ms of delay.

**Separate aim.** The default template already does this: the right mouse
button pulls L2 **and** holds the "aim" layer, whose mouse settings have half
the sensitivity. Change the "aim" layer's mouse settings to tune aiming down
sights on its own; they apply only while the button is held.

### Stick scale (walk, precise aim)

A *Stick scale* output scales a whole stick while its key is held.
**Example:** Left Alt → left stick at 40 % makes W walk instead of run.
Left Ctrl → right stick at 50 % slows the camera for precise aim.

### Macros

A macro is a list of steps. Each step holds some buttons, sticks and
triggers for a time, then the next step follows. A step with nothing held is
a pause. Up to 32 macros.

**Example: "Quick heal"** (open the item wheel, choose up, confirm):

| Step | Holds | For |
|---|---|---|
| 1 | L1 | 150 ms |
| 2 | L1, right stick up | 120 ms |
| 3 | — | 60 ms |
| 4 | Cross | 80 ms |

Bind Q → *Macro: Quick heal*. A macro runs to its end once started. Turn on
*Repeat while held* to loop it for as long as the key is held.

### The suspend key

The suspend key pauses the whole mapping: the game sees only the real
DualSense, and every synthetic button and stick is released at once. Press it
again to resume.

- The default is **Pause/Break**. It can be changed in *Keyboard & mouse →
  Suspend key* to another key or a mouse button, and it can never be used
  by a binding.
- Use it when a profile makes the game unplayable, when you type in a game's
  chat or name field, or when you hand the controller to someone else.
- Any binding can also have the output *Suspend mapping*, for keyboards that
  have no Pause key.

### Tips

- **Game camera sensitivity high.** The stick has a top turn speed. Set the
  game's own look and aim sensitivity to its maximum (or one step below),
  then lower *Sensitivity* here until it feels right. This gives the mouse
  the most room before fast moves are cut off.
- **Game acceleration low.** Set the game's look acceleration, aim
  acceleration or "response curve" to its lowest or linear setting, and its
  deadzone to its lowest. Time-based acceleration in the game cannot be
  undone here; it makes slow aim feel late.
- **Aim assist** works as with a controller; turn it off if it fights the
  mouse.
- **Performance mode.** A game at 60 fps reads the controller twice as often
  as one at 30 fps, so input arrives sooner.
- **Bind by position.** Keys are stored by their physical position (the
  USB key code), so W A S D stay in the same place on AZERTY and QWERTZ
  keyboards.

## Troubleshooting

| Problem | What to check |
|---|---|
| Nothing happens in the game | *Use keyboard & mouse in games* is on; the DualSense is on and belongs to the player in front of the screen; the suspend key was not pressed; the game's profile is not switched off. |
| The mouse works but keys do not (or the other way round) | Unplug and replug that device; the agent tries to open it again every second. A keyboard with extra USB functions may show up as several devices; try another USB port. |
| A game's chat or name field gets no keys | The agent holds the keyboard while the game runs. Type with the on-screen keyboard and the DualSense. |
| A short tap is sometimes missed | Raise the binding's pulse time to 80–100 ms. |
| Aim stutters during slow movement | Raise *Smoothing*. A 125 Hz mouse needs about 20 ms. |
| Aim feels late or floaty | Lower *Smoothing*; set the game's acceleration to its lowest. |
| Small mouse movements do nothing | Raise *Deadzone* (see "Finding a game's deadzone"). |
| The camera drifts when the mouse is still | Lower *Deadzone*. |
| Diagonals snap to the axes | Change *Deadzone shape*. |
| Fast turns are too short | Raise the game's own sensitivity, then lower *Sensitivity* here. |
| Wrong controls after getting into a car | The vehicle layer is out of step with the game; press the key bound to the layer toggle alone. |
| The game pauses with "reconnect the controller" | The DualSense switched off. See "What you need". |
| A game with its own keyboard and mouse support | Use the game's own support and leave the mapping off for it (or suspend it), so both do not act at once. |
| Nothing works in one game only | If the agent could not be started or found no controller reads to hook, the game is left untouched. Restart the game; if it repeats, the game is not supported yet. |

# Part 2: How it works

## The agent in the game

The game is the process that has the screen, so it is the only process the
system sends keyboard and mouse input to. The mapping therefore runs inside
the game.

When a game starts and the feature is on, the daemon waits until the game
has loaded libScePad, then injects a small agent (`kbm_agent.elf`, embedded
in the daemon) into the game process with the same injector that starts the
Toolbox in ShellUI (`libNineS`). The agent runs on its own thread. Its main
function never returns, because a payload returning would end the game.
Nothing is written to the game's files; everything lives in the game's
memory and goes away when it closes.

## Mouse

The agent loads libSceMouse from the game's own sandbox path
(`/<sandbox word>/common/lib/`; the word comes from
`sceKernelGetFsSandboxRandomWord()`), opens the mouse for the player in
merged mode, and reads it on its own thread (about 250 samples a second).
Motion and button changes are time-stamped as they arrive and handed to the
engine.

## Keyboard

libSceKeyboard never delivers keys on the PS5, in a game or in ShellUI. Keys
come only through the system IME's keyboard. So the agent loads libSceIme,
opens the player's IME keyboard with its own handler (when the game already
holds it, the agent closes it and opens it again for itself), and calls
`sceImeUpdate` from its own thread to pump the events. Key down and key up
events carry the USB HID key code, which is what bindings store. The game's
own IME pump then only gets "not open", which games tolerate. If the
keyboard is taken away, the agent opens it again a second later.

The mouse and keyboard follow the user in front of the screen: when the
foreground user changes, both are closed and opened again for the new user.

## Hooking the controller reads

Games read the controller with `scePadReadState`, `scePadRead`,
`scePadReadStateExt` or `scePadReadExt`. The agent finds the address of each
in libScePad by name, then scans the non-executable segments of the game's
main module **and every other module it has loaded** for import slots that
hold those addresses, makes each slot writable and swaps in its hook with one
atomic store. No game code is changed, and code pages (execute-only) are
never read. The patched slots and their original values are recorded in the
control page, so a later agent can restore or take them over.

Each hook calls the real function first, then merges:

- If the read failed, or the system has the controller (the
  intercepted bit `0x80000000` is set: Home, the Control Center, a system
  dialog), the real data is returned untouched.
- **Buttons:** real OR mapped.
- **Sticks:** for each stick axis, the value further from the centre wins,
  so the DualSense works the moment it is touched.
- **Triggers:** the larger value.
- Everything else (connection, touchpad, motion, time stamps) is the real
  controller's.
- `scePadRead` and `scePadReadExt` return several samples; every sample is
  merged, not only the last.
- A profile with the real controller merge turned off replaces the
  buttons, sticks and triggers outright instead of merging them.

The engine runs inside the hook, once per game read. It turns the latest
keyboard and mouse snapshot into a controller state (layers, gestures,
modes, chords, SOCD, ramps, macros, mouse to stick), with no memory
allocation, no locks and no logging, so it cannot stall the game's input.

## The control page

The agent maps one page at a fixed address in the game (only if nothing is
mapped there). It holds:

- a magic value, the agent's generation and the profile generation;
- the agent's status (running, hooked, mouse open, keyboard open) and a
  heartbeat that grows on every input loop;
- the patched import slots and their originals;
- the compiled profile.

## Profile delivery

The Toolbox saves profiles as JSON files and tells the daemon that one has
changed. The daemon reads the running game's profile (or the default
template), checks and clamps every value, and compiles it into one flat
block with no pointers. It writes the block into the control page with
kernel memory writes, then increases the profile generation. The agent
copies the profile when it sees a new generation, at a moment when no read
is using it, and resets all outputs so nothing stays pressed. No data goes
over a socket while the game is running.

## Hot replacement

When the HEN is reloaded, a new agent can be injected into a game that
already has one. The new agent finds the control page, takes the next
generation and takes over the patched slots. The old agent sees the newer
generation, closes its mouse and keyboard and sleeps; it does not return.
The game never runs without a hook in between and is never hooked twice.

## Every firmware

Nothing depends on a firmware version. Libraries are found by name in the
game's sandbox, functions by name or NID, import slots by their value,
modules by the system's own module list, and structures by their size. The
same build works on every firmware the HEN runs on.

## Safety

- The feature is off until it is turned on, and it never touches a game
  while the HEN starts.
- Fail safe means pass-through: if the control page is missing or damaged,
  the profile is invalid or turned off, the system has the controller, or
  the agent's input loop has not run for 200 ms, the game gets the real
  controller data unchanged.
- When the mapping is suspended, the profile changes, or the system takes
  the controller, every mapped output is released at once. A key that is
  still held afterwards must be released before it counts again.
- The suspend key always works and cannot be bound.
- The real DualSense keeps working at all times.
- Profiles are checked when they are compiled: out-of-range numbers are
  clamped, bad bindings are dropped, and the rest still loads.
- Nothing is written to system or game files. Profiles live under
  `/data/AniketosHEN/kbm`.

# Part 3: Files and settings

| Path | Content |
|---|---|
| `/data/AniketosHEN/kbm/settings.json` | global switch and suspend key |
| `/data/AniketosHEN/kbm/default.json` | the default template; created from the built-in one when missing |
| `/data/AniketosHEN/kbm/titles/<TITLEID>.json` | one game's profile, e.g. `titles/PPSA04264.json` |

A game uses `titles/<its title ID>.json` if it exists, otherwise
`default.json`, otherwise the built-in template. Title IDs that become file
names may hold only `A`–`Z`, `0`–`9` and `_` (up to 16). Files are saved
through a temporary file and a rename, so a crash never leaves half a file.
Files larger than 256 KiB are refused.

Files can be edited or copied over FTP. A file changed that way is used the
next time the game starts; changes made in the Toolbox reach a running game
at once.

## settings.json

```json
{
  "schema": 1,
  "enabled": true,
  "suspend_key": "key:Pause"
}
```

| Key | Meaning | Default |
|---|---|---|
| `enabled` | the global switch, *Use keyboard & mouse in games* | `false` |
| `suspend_key` | the suspend key: a key or a mouse button (not the wheel or motion) | `key:Pause` |

## Names

Names ignore case. Keys are USB HID key codes, so they name a physical
position, not a character.

**Inputs**

| Kind | Form | Examples |
|---|---|---|
| Key | `key:<name>`; a key without a name as `key:0x<code>` | `key:W`, `key:Space`, `key:LShift`, `key:F1`, `key:Pause`, `key:0x87` |
| Mouse button | `mouse:<name>` | `mouse:left`, `mouse:right`, `mouse:middle`, `mouse:back`, `mouse:forward`, `mouse:6` to `mouse:8` |
| Wheel notch | `wheel:<direction>` | `wheel:up`, `wheel:down`, `wheel:left`, `wheel:right` |
| Mouse motion | `move:<direction>` | `move:right`, `move:left`, `move:down`, `move:up` |

Key names: `A`–`Z`, `0`–`9`, `F1`–`F24`, `Enter`, `Escape`, `Backspace`,
`Tab`, `Space`, `Minus`, `Equal`, `LBracket`, `RBracket`, `Backslash`,
`Semicolon`, `Quote`, `Grave`, `Comma`, `Period`, `Slash`, `CapsLock`,
`PrintScreen`, `ScrollLock`, `Pause`, `Insert`, `Home`, `PageUp`, `Delete`,
`End`, `PageDown`, `Up`, `Down`, `Left`, `Right`, `NumLock`, `KP0`–`KP9`,
`KPDivide`, `KPMultiply`, `KPMinus`, `KPPlus`, `KPEnter`, `KPPeriod`,
`LCtrl`, `LShift`, `LAlt`, `LGui`, `RCtrl`, `RShift`, `RAlt`, `RGui`, and a
few more (`kbm_names.cpp`).

**Outputs**

| Output | Form | Examples |
|---|---|---|
| Nothing | `none` | `none` |
| Buttons | `pad:<button>[+<button>...]` | `pad:cross`, `pad:l3+r3`, `pad:up`, `pad:options`, `pad:touchpad` |
| Stick direction | `axis:<l\|r><x\|y><-\|+>`; Y is down-positive, so `ly-` is up | `axis:ly-` (left stick up), `axis:lx+` (left stick right) |
| Trigger | `trigger:l2`, `trigger:r2` | |
| Layer | `layer:hold`, `layer:toggle`, with the layer's name in `layer` | |
| Macro | `macro`, with the macro's name in `macro` | |
| Stick scale | `scale:left`, `scale:right` | |
| Suspend | `suspend` | |

Button names: `cross`, `circle`, `square`, `triangle`, `l1`, `r1`, `l2`,
`r2`, `l3`, `r3`, `options`, `touchpad`, `up`, `down`, `left`, `right`.

## Example

A complete profile, `titles/PPSA01234.json`. It uses every kind of field:
a base layer, a toggled vehicle layer with racing ramps and mouse steering, a
held aim layer, gestures, modes, a chord and a macro.

```json
{
  "schema": 1,
  "name": "My game",
  "enabled": true,
  "merge_real_pad": true,
  "trigger_button_threshold": 30,
  "layers": [
    {
      "name": "base",
      "mouse": {
        "inherit": false,
        "mode": "velocity",
        "stick": "right",
        "deadzone_shape": "circular",
        "invert_x": false,
        "invert_y": false,
        "sensitivity": 1.0,
        "yx_ratio": 1.0,
        "exponent": 1.0,
        "deadzone": 0.2,
        "outer": 1.0,
        "smoothing_ms": 12,
        "boost": 1.3,
        "position_range": 400,
        "spring_per_s": 3.0
      },
      "opposing": "last",
      "mouse_digital_threshold": 2000,
      "bindings": [
        { "input": "key:W", "outputs": ["axis:ly-"] },
        { "input": "key:S", "outputs": ["axis:ly+"] },
        { "input": "key:A", "outputs": ["axis:lx-"] },
        { "input": "key:D", "outputs": ["axis:lx+"] },
        { "input": "key:LAlt", "label": "Walk",
          "outputs": [{ "out": "scale:left", "value": 0.4 }] },
        { "input": "key:LShift", "label": "Sprint", "mode": "turbo",
          "turbo_hz": 8, "turbo_duty": 50, "outputs": ["pad:cross"] },
        { "input": "key:LCtrl", "label": "Crouch", "mode": "toggle",
          "outputs": ["pad:circle"] },
        { "input": "key:Space", "outputs": ["pad:square"] },
        { "input": "mouse:left", "outputs": ["trigger:r2"] },
        { "input": "mouse:right", "label": "Aim",
          "outputs": ["trigger:l2", { "out": "layer:hold", "layer": "aim" }] },
        { "input": "wheel:up", "pulse_ms": 80, "outputs": ["pad:right"] },
        { "input": "wheel:down", "pulse_ms": 80, "outputs": ["pad:left"] },
        { "input": "key:Escape", "gesture": "tap", "tap_ms": 200,
          "outputs": ["pad:options"] },
        { "input": "key:Escape", "gesture": "long", "long_ms": 400,
          "outputs": ["pad:touchpad"] },
        { "input": "key:1", "outputs": ["pad:up"] },
        { "input": "key:1", "modifiers": ["key:LShift"], "label": "Use item 1",
          "outputs": ["pad:up+cross"] },
        { "input": "key:Q", "label": "Quick heal", "mode": "pulse",
          "outputs": [{ "out": "macro", "macro": "quick_heal" }] },
        { "input": "key:F", "label": "Enter vehicle",
          "outputs": ["pad:triangle", { "out": "layer:toggle", "layer": "vehicle" }] },
        { "input": "key:CapsLock", "label": "Fix vehicle layer",
          "outputs": [{ "out": "layer:toggle", "layer": "vehicle" }] },
        { "input": "key:F8", "label": "Suspend (no Pause key)",
          "outputs": ["suspend"] }
      ]
    },
    {
      "name": "vehicle",
      "mouse": {
        "inherit": false,
        "mode": "position",
        "stick": "left",
        "deadzone_shape": "circular",
        "invert_x": false,
        "invert_y": false,
        "sensitivity": 1.0,
        "yx_ratio": 0.1,
        "exponent": 1.0,
        "deadzone": 0.1,
        "outer": 1.0,
        "smoothing_ms": 12,
        "boost": 1.0,
        "position_range": 800,
        "spring_per_s": 0.5
      },
      "opposing": "last",
      "mouse_digital_threshold": 2000,
      "bindings": [
        { "input": "key:W", "label": "Throttle",
          "outputs": [{ "out": "trigger:r2", "value": 1.0,
                        "rise_ms": 300, "fall_ms": 100, "curve": 2.0 }] },
        { "input": "key:S", "label": "Brake",
          "outputs": [{ "out": "trigger:l2", "value": 1.0,
                        "rise_ms": 150, "fall_ms": 0, "curve": 1.0 }] },
        { "input": "key:A", "label": "Steer left",
          "outputs": [{ "out": "axis:lx-", "value": 1.0,
                        "rise_ms": 200, "fall_ms": 120, "curve": 1.0 }] },
        { "input": "key:D", "label": "Steer right",
          "outputs": [{ "out": "axis:lx+", "value": 1.0,
                        "rise_ms": 200, "fall_ms": 120, "curve": 1.0 }] },
        { "input": "key:Space", "label": "Handbrake", "outputs": ["pad:r1"] },
        { "input": "key:LShift", "outputs": ["none"] },
        { "input": "key:LAlt", "outputs": ["none"] }
      ]
    },
    {
      "name": "aim",
      "mouse": {
        "inherit": false,
        "mode": "velocity",
        "stick": "right",
        "deadzone_shape": "circular",
        "invert_x": false,
        "invert_y": false,
        "sensitivity": 0.5,
        "yx_ratio": 1.0,
        "exponent": 1.0,
        "deadzone": 0.2,
        "outer": 1.0,
        "smoothing_ms": 12,
        "boost": 1.0,
        "position_range": 400,
        "spring_per_s": 3.0
      },
      "opposing": "last",
      "mouse_digital_threshold": 2000,
      "bindings": []
    }
  ],
  "macros": [
    {
      "name": "quick_heal",
      "repeat_while_held": false,
      "steps": [
        { "ms": 150, "buttons": "l1" },
        { "ms": 120, "buttons": "l1", "ry": -1.0 },
        { "ms": 60 },
        { "ms": 80, "buttons": "cross" }
      ]
    }
  ]
}
```

### Profile

| Key | Meaning | Default |
|---|---|---|
| `schema` | file format version; a file with a newer one is refused | `1` |
| `name` | shown in the Toolbox | |
| `enabled` | `false` passes the real controller through untouched | `true` |
| `merge_real_pad` | `true`: the DualSense works alongside; `false`: mapped buttons, sticks and triggers replace the controller's | `true` |
| `trigger_button_threshold` | trigger value (0–255) from which L2/R2 also set their button | `30` |
| `layers` | up to 8; the first is the base layer | |
| `macros` | up to 32 | |

### Layer

| Key | Meaning | Default |
|---|---|---|
| `name` | used by `layer:hold` / `layer:toggle` outputs | |
| `mouse` | mouse settings, below | |
| `opposing` | SOCD: `last`, `neutral` or `first` | `last` |
| `mouse_digital_threshold` | mouse speed (counts per second) from which `move:*` inputs count as held | `2000` |
| `bindings` | up to 256 in the whole profile | |

### Mouse

| Key | Meaning | Range | Default |
|---|---|---|---|
| `inherit` | use the mouse settings of the layer below (not on the base layer) | | `false` |
| `mode` | `off`, `velocity` or `position` | | `velocity` |
| `stick` | `left` or `right` | | `right` |
| `deadzone_shape` | `circular` or `square` | | `circular` |
| `invert_x`, `invert_y` | flip an axis | | `false` |
| `sensitivity` | Velocity: gain | 0.01–50 | 1.0 |
| `yx_ratio` | vertical gain relative to horizontal | 0.1–5 | 1.0 |
| `exponent` | response curve; below 1 boosts small moves | 0.25–4 | 1.0 |
| `deadzone` | the game's inner deadzone to skip | 0–0.5 | 0.2 |
| `outer` | deflection at which the output is full | 0.5–1 | 1.0 |
| `smoothing_ms` | Velocity: speed window | 4–100 | 12 |
| `boost` | Velocity: flick gain and carry; 1 is off | 1–4 | 1.0 |
| `position_range` | Position: mouse counts for full deflection | 50–20000 | 400 |
| `spring_per_s` | Position: return to centre, full deflection per second; 0 is none | 0–100 | 3.0 |

### Binding

| Key | Meaning | Range | Default |
|---|---|---|---|
| `input` | an input name (see Names) | | required |
| `modifiers` | up to 2 input names that must also be held (chord) | | none |
| `label` | free text shown in the Toolbox | | |
| `gesture` | `press`, `tap`, `long`, `double` or `release` | | `press` |
| `mode` | `hold`, `toggle`, `turbo` or `pulse` | | `hold` |
| `turbo_hz` | turbo rate per second | 1–30 | 10 |
| `turbo_duty` | turbo percent on | 10–90 | 50 |
| `tap_ms` | a tap must be released within this | 50–2000 | 200 |
| `long_ms` | a long press must be held this long | 100–5000 | 400 |
| `double_ms` | the second press of a double tap within this | 100–2000 | 250 |
| `pulse_ms` | length of the pulse of tap, release, pulse mode and wheel | 16–2000 | 60 |
| `outputs` | 1 to 4 outputs | | required |

The same input, gesture and modifiers twice in one layer: the later binding
wins. The suspend key cannot be bound; such a binding is dropped.

### Output

An output is either a bare name (`"pad:cross"`, `"axis:ly-"`, `"none"`,
`"suspend"`) or an object with `out` set to the name and these keys:

| Key | For | Meaning | Default |
|---|---|---|---|
| `value` | `axis:*`, `trigger:*` | how far, 0–1 | 1.0 |
| `value` | `scale:*` | stick scale, 0–1 | 0.5 |
| `rise_ms`, `fall_ms` | `axis:*`, `trigger:*` | ramp times, 0–10000 | 0 |
| `curve` | `axis:*`, `trigger:*` | ramp shape, 0.1–10; above 1 starts slowly | 1.0 |
| `layer` | `layer:hold`, `layer:toggle` | the layer's name | |
| `macro` | `macro` | the macro's name | |

An output that names a missing layer or macro is dropped.

### Macro

| Key | Meaning | Default |
|---|---|---|
| `name` | used by `macro` outputs | |
| `repeat_while_held` | loop while the key is held | `false` |
| `steps` | up to 512 in the whole profile | |

Each step: `ms` (1–10000) is how long it lasts; `buttons` (`"l1+cross"`) are
held for that time; `lx`, `ly`, `rx`, `ry` (-1 to 1, Y down-positive) set a
stick axis, and an axis left out is not touched; `l2`, `r2` (0–255) pull a
trigger. A step with none of them is a pause.

### Checking

When a profile is loaded, numbers out of range are clamped, unknown names
and bindings without a valid input or output are dropped, and anything over
the limits is cut off; each change gives one warning, and the rest of the
profile still works. Only a file that does not parse, is not a JSON object,
or has a newer `schema` is refused; the game then uses `default.json` or the
built-in template.
