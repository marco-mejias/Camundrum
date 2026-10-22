# Camundrum — Game Feel

How the feel changes work: what they do, where they live and how to tune them. Companion to [PLAN.md](PLAN.md) and [ARCHITECTURE.md](ARCHITECTURE.md).

There are two groups: how the player moves, in [scripts/PlayerController.cpp](scripts/PlayerController.cpp), and how a view switch feels, spread over the GameManager, the DepthController and the CameraController.

---

## 1. Player movement

### What it stores

| Data | What for |
|---|---|
| `input_axis` | the direction the GameManager handed over this frame |
| `jump_requested` | the jump key was just pressed and has not been processed yet |
| `jump_held` | the jump key is still down, which decides the jump height |
| `is_jump_rising` | a jump is on its way up and can still be cut short |
| `coyote_left` | seconds left in which a jump is still allowed after leaving the ground |
| `buffer_left` | seconds left in which a remembered jump press is still valid |
| `is_frozen` | the player is paused while the level slides |

The speed itself is not stored by the script. It lives in the body and is read back each frame, which is what lets it build up and wear off.

### How one frame goes

1. The InputController samples the keyboard: direction, jump just pressed, jump still held.
2. The GameManager reads those three values and passes them on with `SetMoveInput`.
3. `OnPhysicsProcess` picks the movement function for the current view.
4. That function builds the new velocity with two helpers:
   - `Steer` for each axis the player controls.
   - `SolveVertical` for gravity and the jump, in side and iso only.
5. The body moves and collides with that velocity.

### Acceleration and braking

`Steer` moves the current speed towards the speed the input asks for, by a limited amount each frame, instead of setting it directly.

| Situation | Setting used | Time to change by full speed |
|---|---|---|
| On the ground, a direction is held | `ground_acceleration` (70) | about 0.09 s |
| On the ground, no direction | `ground_deceleration` (90) | about 0.07 s |
| In the air | `air_acceleration` (40) | about 0.15 s |

- Braking is quicker than speeding up, so the player stops where you let go.
- The air gives less control, so a jump keeps most of its direction.
- `Approach` is the small helper underneath: it steps a value towards a target and never overshoots it.

### Gravity

`SolveVertical` applies a different pull depending on the direction of travel.

- **Going up:** `rise_gravity` (25).
- **Going down:** `fall_gravity` (45), almost twice as strong, so the fall is quick and the jump does not feel floaty.
- **Speed cap:** the fall never goes faster than `max_fall_speed` (22).

### The jump

A jump starts when two timers are both above zero.

| Timer | Refilled when | Runs down |
|---|---|---|
| `coyote_left` | every frame the player stands on something, to `coyote_time` (0.1 s) | while in the air |
| `buffer_left` | the jump key is pressed, to `jump_buffer_time` (0.12 s) | every frame after that |

That single rule gives three behaviours:

- **Normal jump:** on the ground and the key was just pressed.
- **Coyote time:** just walked off a ledge, the first timer has not run out yet, so the jump still fires.
- **Jump buffer:** the key was pressed slightly before landing, the second timer is still running on touchdown, so the jump fires on the first grounded frame.

When the jump fires, both timers are emptied so it cannot fire twice, the vertical speed is set to `jump_speed` (9), and `is_jump_rising` is set.

### Variable jump height

- While `is_jump_rising` is set and the key is released, the upward speed is multiplied by `jump_cut` (0.45) once. The jump ends early.
- Holding the key leaves the jump alone, and it reaches its full height.
- The flag clears by itself at the top of the jump, so releasing the key on the way down does nothing.

### What each view uses

| | Side | Top | Iso |
|---|---|---|---|
| `Steer` on | X | X and Y | X and Z |
| Acceleration | ground or air | always ground | ground or air |
| Gravity and jump | yes | no | yes |
| Jump press | buffered | dropped | buffered |

In top view the board is the floor, so the player always has ground control and a jump press is thrown away instead of waiting for another view.

### When the jump state is cleared

`ClearJumpState` empties both timers and the rising flag:

- when entering or leaving top view, together with the velocity, because top view has no momentum;
- on `Halt`, when the level restarts;
- when the player leaves the scene.

While the player is frozen during a slide, a jump press is dropped and the velocity is kept, so a jump interrupted by a switch continues afterwards.

### Settings in the inspector

All on the `Player` node.

| Property | Value | Effect of raising it |
|---|---|---|
| `move_speed` | 6.0 | faster top speed |
| `ground_acceleration` | 70.0 | reaches top speed sooner |
| `ground_deceleration` | 90.0 | stops sooner |
| `air_acceleration` | 40.0 | more steering in the air |
| `jump_speed` | 9.0 | higher jump |
| `rise_gravity` | 25.0 | lower, shorter jump |
| `fall_gravity` | 45.0 | faster fall |
| `max_fall_speed` | 22.0 | higher falling speed cap |
| `jump_cut` | 0.45 | a released jump keeps more height (1 disables the cut) |
| `coyote_time` | 0.1 | more forgiving ledges |
| `jump_buffer_time` | 0.12 | more forgiving landings |

### Measured in the test run

| Check | Result |
|---|---|
| Speed 2 frames after pressing a direction | 2.33 of 6 |
| Speed 10 frames after | 6 |
| Speed 2 frames after releasing | 3 |
| Peak height, key held | 2.2 (player centre, standing height is 0.5) |
| Peak height, short tap | 1.1 |
| Jump pressed 3 frames after leaving a ledge | jumped |
| Jump pressed in the air 5 frames before landing | jumped 1 frame after landing |

---

## 2. View switch

### What happens on a switch

1. The GameManager gives the player the rules of the new view.
2. It asks the DepthController to flatten or restore the level. If a slide starts, the GameManager freezes the player and marks the switch as in progress.
3. It tells the CameraController to move to the new pose.
4. When the slide ends, the DepthController emits `move_finished` and the GameManager releases the player.

Side to top has no slide, because both are flat, so nothing is frozen.

### Queued switch

Stored in the GameManager as `queued_view`.

- A view key pressed during a slide is not lost: the view is kept in `queued_view`.
- Only the latest request is kept. Pressing two keys during one slide keeps the second.
- When `move_finished` arrives, the queued view is entered straight away, unless it is the view already active.
- A restart clears the queue.

### Background colour per view

Handled by the CameraController, in the same function that blends the camera.

- Each view has its own colour for the empty space behind the level. The board keeps its own colour.
- During a transition the colour blends from the one shown now to the target, with the same easing and duration as the camera.
- A switch made in the middle of another starts from the colour currently on screen, so there is no jump.

| Property on `CameraController` | Value |
|---|---|
| `side_background` | dark blue |
| `top_background` | brown |
| `iso_background` | dark teal |
| `transition_time` | 0.35 s |

### Measured in the test run

| Check | Result |
|---|---|
| Pressed 3, then 1 during the slide | slid to iso, then back to side |
| Background in side, top and iso | three different colours, matching the settings |

### Death feedback

Handled by the GameManager, with the player, the CameraController and the new UIController.

1. The player reports a death: falling out of the level, a hazard, or being crushed after a switch.
2. The whole world freezes and the player vanishes.
3. The camera shakes for 0.3 s, fading out (`Shake` on the CameraController).
4. The screen fades to black over `death_time` (0.45 s, on the `Game` node).
5. The level restarts, and the screen clears again over 0.3 s.

Pressing R during the fade restarts at once.

Each cause has its own animation before the fade, so the player can tell what happened:

| Cause | Animation |
|---|---|
| Spikes | the player turns red, blinks and is thrown up a little |
| Board hole | the player shrinks away into the hole |
| Falling out of the level | the player vanishes |

### Blocked switch

A view switch that would leave the player or a crate inside something does not happen. The void never blocks a switch: it is part of the scenery. Instead the element and whatever is in the way glow red, pulsing, for half a second. The same applies to a lever turn that would push something into a wall.

A flat view also refuses to hide a lever: pieces in the `keep_clear` group mark what must stay visible with a switched off `Footprint` box, and if a fixed solid would cover it across the board, the switch is refused and both glow. The player and crates never count as covering, since they can move away. Wall buttons are the answer for controls behind walls: they sit on a face, so they are always in sight.

---

## 3. Reading the space

| Aid | Where | How it works |
|---|---|---|
| Shadow | player and crates, iso only | a dark disc on the first surface straight below, smaller the higher the body is |
| X-ray dots | the player, any view | the parts of the player hidden behind something are drawn as a grid of orange dots on top of it (`shaders/xray_dots.gdshader`, on the `Mesh/XRay` child) |
| View indicator | top-left corner | the active view is drawn in its colour, the other two faded |
| Key hint | bottom-left corner | E and R |
| Red glow | anything in the way of a switch or a turn | a red layer drawn over the piece, pulsing |

The see-through depth hint was removed: it confused more than it helped.

The x-ray works with the depth of the scene already drawn: the `XRay` copy of the player's mesh is drawn last, with no depth test, and keeps only the pixels where something nearer was drawn first, and of those only round dots on a fixed screen grid. Where the player is in plain sight nothing is added. `tint`, `spacing` and `dot_radius` on the material change the look. Copying the `XRay` node onto a crate would work the same way.

---

## 4. Juice

Small touches that make every action answer back. None of them change the rules.

### The player's body

Done in `UpdateLook`, in the player's `OnProcess`, so it runs at the screen's rate and keeps going while the world is frozen.

| Touch | How |
|---|---|
| Stretch on a jump | `Kick(4.5)` pushes a spring, the body grows tall and thin and swings back |
| Squash on landing | `Kick` with a negative push, harder the faster the fall; the feet stay on the ground |
| Lean | the body tilts a little into its horizontal speed (side and iso; not in top) |
| Dust | the `Dust` particles under the feet puff on every jump and on hard landings |
| Into the exit | the body spins and shrinks away (`ShowExit`) |

The spring is `squash` and `squash_speed`: each frame it is pulled back to zero with a stiffness of 260 and a damping of 18, which settles in about a quarter of a second with one small wobble.

### Sounds

All played through the AudioController (see ARCHITECTURE.md). The files in `audio/` are synthesized by a script and can be replaced by real ones with the same names.

| Sound | When |
|---|---|
| `jump`, `land` | the player jumps, or lands after a real fall |
| `switch` | a view switch starts |
| `blocked` | a switch or a turn is refused, together with a light camera knock |
| `lever`, `button`, `plate` | a lever is pulled, a wall button pressed, a plate pressed down |
| `door` | a door starts opening or closing |
| `turn` | a turntable or layer starts turning; a light knock when it lands |
| `crate_land` | a crate lands after a real fall |
| `death` | the player dies |
| `exit` | the level is finished |

### Elements

| Touch | Where |
|---|---|
| Doors sink into the ground and rise again over 0.18 s instead of vanishing; the collision still changes at once | `Door.cpp`, `OnProcess` |
| The lever handle swings over instead of jumping | `Lever.cpp`, `OnProcess` |
| The exit breathes, and bursts into sparks when reached | `Exit.cpp` |
| Water is drawn with light bands that slide along the flow at the river's speed and stop in top, where the water is still; the fall hugs the cliff from the ground's edge down | `shaders/water_flow.gdshader`, `Water.cpp` (`phase`), `RiverSource.cpp` |
| A crate the current throws off an edge keeps drifting the way the water carried it while it falls, instead of dropping straight down | `Crate.cpp` (`drift`) |
| The river changes shape with the view instead of jumping: leaving iso, the water on the ground drains off the edge and the last of it goes down the fall; entering iso, it runs out from the board along the ground and then pours over the edge. The ground part takes 70% of `change_time` (0.5 s on the river source), the fall the rest, and the board stretch deepens or thins at the same time. Foam splashes where the board stretch meets the floor in side, and where the fall lands in iso | `RiverSource.cpp`, `OnProcess` and `ShowWater` |
| The "level complete" message pops in, overshooting a little | `UIController.cpp`, `ApplyPop` |

### Camera

| Touch | Where |
|---|---|
| During a view switch the zoom dips by 5% at the halfway point and comes back, a small punch | `ApplyBlend` |
| Light knocks on a refused action and at the end of a turn; a stronger shake on death | `Shake` |
| Each level is framed when it loads: the views keep their angles, centre on the level and zoom out if it does not fit, never in closer than the poses in `game.tscn` | `FrameLevel`, `FitPose` |

### Top is seen from above

Side and top cameras both face the board, so on their own the two views look almost the same. Top is made to read as a view from above:

- **Pieces turn towards the camera.** The player, the robot, the flag and levers are drawn turned a quarter turn in top, so their up points at the viewer: the robot's antenna and the flag's pole show as dots in the middle. Only the look turns, never the body. The turn follows the camera move (0.35 s, eased) and jumps straight to place when a level starts or restarts. Shared helpers in `ElementHelpers.h`: `StepTopTurn`, `TopTurnAngle`, `TurnedForTop`; pieces on the ground turn around the middle of the cell above it, so seen from above they fill that cell.
- Ground plates, crates and blocks are left as they are.
- **The light comes from over the camera.** The level's `Light` turns towards the camera in top and back in the other views, blended with the camera move (CameraController).

### Colours

Each view has a strong background of its own: blue in side, red in top, green in iso (`side_background`, `top_background`, `iso_background` on the CameraController). A `WorldEnvironment` in `game.tscn` adds soft ambient light (0.22) and a little extra contrast and saturation, so pieces read clearly. Boards are a brighter blue, blocks a light grey, crates a wood brown kept apart from the player's orange.

### Neon background

Many small white neon pyramids drift slowly left to right behind the level and turn, read differently in each view like everything else: in iso they are whole pyramids turning on themselves; in side they are triangles rolling like wheels (pressed flat in depth, because the side camera looks slightly down); in top their tip faces the camera and they turn on the spot. The change follows the camera move. The glow comes from the `WorldEnvironment` (glow threshold 1.0); the lines are drawn brighter than that, so the level itself does not glow.

### Menus and in-game messages

The menus move: each one fades in and its pieces come in one after another, the title's letters drop in and then wave with colours drifting through the three views, and the focused button or slider grows a little with a soft tick. Pressing sounds a confirm, going back a softer sound, on their own `Interface` volume. An options screen (main and pause menu) sets the master, effects and interface volumes, the screen shake and fullscreen, saved between sessions.

In game, on a view switch the active label of the view bar hops; a level shows its number and name for a moment when it starts. The camera shakes are scaled by the screen shake option. Details in ARCHITECTURE.md, UIController and SettingsController.

### Controls during a turn

While a turntable or a layer turns, and while the turned pieces slide back to the front edge, the player cannot move, use anything or ask for a view: those requests are dropped, not queued. When it is over the player starts from rest (`StopMotion`), so no speed from before the turn carries into the new layout. A view switch alone still keeps the latest view key pressed during its slide.

---

## 5. Details imposed by Jenova

- `SetMoveInput` takes four plain numbers (two for the direction, two 0/1 flags) because calls between scripts cannot carry a `Vector2` or a `bool`.
- The jump press sets a flag in `SetMoveInput` and the timer is filled later, inside `OnPhysicsProcess`. Properties such as `jump_buffer_time` may be out of date inside a function another module calls.
- The colour properties have a default in the script, but Jenova delivers colour defaults as black. The real values are stored on the node in `game.tscn`; a new scene with a CameraController needs them set by hand.
- The slide time exists twice, as `transition_time` on both `CameraController` and `DepthController`. Change them together.

---

## 6. Not done yet

| Item | Block in the plan |
|---|---|
| Short pause (hit-stop) on a switch | 3 |
| Menu, level flow and saved progress | 7 |
| Controls relative to the camera in iso | open decision |
