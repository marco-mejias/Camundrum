# Camundrum — Architecture

Companion to [PLAN.md](PLAN.md). This describes the modules, what each one owns, and how they talk. Nothing here is built yet.

---

## 1. Philosophy

- **One module, one job.** Each controller owns a single concern and knows nothing about the others.
- **The GameManager is the only connector.** Controllers never hold references to each other.
- **Commands go down, events go up.**
  - The GameManager *calls methods* on controllers ("switch to top", "respawn").
  - Controllers *emit signals* to the GameManager ("player died", "transition finished").
- **Level elements wire to each other locally.** A plate opening a door is a signal connection made in the level scene. It does not pass through the GameManager, which only hears game-level events (death, exit reached).

```
					┌──────────────────┐
	 signals up     │   GameManager    │    method calls down
   ┌───────────────▶│  state + routing │────────────────┐
   │                └──────────────────┘                │
   │                                                    ▼
┌──┴───────┬────────────┬────────────┬───────────┬──────────┬────────┐
│  Input   │   Player   │   Camera   │   Depth   │  Level   │   UI   │
└──────────┴────────────┴────────────┴───────────┴────┬─────┴────────┘
													  │ owns
											  ┌───────┴────────┐
											  │ Level elements │
											  │ (plate ─▶ door)│
											  └────────────────┘
```

---

## 2. Modules

### GameManager

The hub. Owns the two pieces of global state and every cross-module sequence.

| | |
|---|---|
| **Owns** | Current view (`Side`, `Top`, `Iso`). Game state (`Loading`, `Playing`, `Switching`, `Dead`, `Complete`, `Paused`) |
| **Listens to** | every controller's signals |
| **Calls** | every controller's methods |
| **Never does** | read input, move anything, touch a camera, know what a crate is |

### InputController

Turns raw keys into intentions, so no other module reads the keyboard.

| | |
|---|---|
| **Owns** | Key bindings |
| **Emits** | `view_requested(view)`, `restart_requested`, `pause_requested`, `interact_requested` |
| **Exposes** | `GetMoveAxis()` (a 2D vector), `IsJumpPressed()`, polled each frame |
| **Never does** | decide whether an action is allowed |

### PlayerController

Moves the character. Attached to the player's `CharacterBody3D`.

| | |
|---|---|
| **Owns** | Velocity, grounded state, the three movement modes |
| **Receives** | `SetMode(view)`, `SetMoveInput(axis, jump)`, `Respawn(position, depth)`, `Freeze(on)`, `Kill()` |
| **Emits** | `died(reason)`, `landed`, `jumped` |
| **Never does** | read keys, change the view, move the camera, store its own remembered depth |

Movement per mode:

| Mode | Input axis maps to | Gravity | Locked axis |
|---|---|---|---|
| Side | X, jump on Y | on | Z |
| Top | X and Y | off | Z |
| Iso | X and Z, jump on Y | on | none |

### CameraController

Everything the player sees about a view.

| | |
|---|---|
| **Owns** | One orthographic camera, three stored poses, three background colours, the transition animation |
| **Receives** | `SetTarget(node)`, `TransitionTo(view)`, `SnapTo(view)` |
| **Emits** | `transition_finished(view)` |
| **Never does** | decide when to switch, affect gameplay |

- **Side pose:** faces the board with a slight tilt, so depth is hinted at.
- **Top pose:** faces the board straight on.
- **Iso pose:** angled from above and to the side.
- **Background colour:** one colour per view, blended during the transition. This is the sky behind the level, not the board.

All three views are orthographic. Each pose is its own `Camera3D` node (`SidePose`, `TopPose`, `IsoPose`) so it can be previewed and adjusted in the editor; a fourth camera, the only one that renders, blends between them.

### DepthController

The flattening rules. The most game-specific module.

| | |
|---|---|
| **Owns** | The remembered depth and starting position of every registered element, the blinking of whatever is in the way |
| **Receives** | `RegisterLevel(level)`, `SetView(view)`, `CanSwitchTo(view)`, `Flatten(instant)`, `Restore(instant)`, `PrepareRotation(carrier)`, `FinishRotation(carrier)`, `ResetAll()` |
| **Emits** | `move_finished` when an animated move ends |
| **Never does** | decide to switch, kill the player |

- **Which elements:** direct children of the level that are in the `depth` group. Each can carry a `depth` metadata value saying how many units deep it is (default 1).
- `Flatten(instant)`: store each element's depth, then push it to the front edge of the level, so its front face lines up with that edge. The edge is the level root's `flat_front_z` metadata (4.5 in the first level), so a 1-deep element ends at `z = 4`.
- `Restore(instant)`: put each element back at its stored depth, keeping its current X and Y.
- **Animated moves:** unless `instant` is 1, elements slide in depth over `transition_time` with the same easing as the camera. Both calls return whether a move started. The GameManager then freezes the player and ignores view requests until `move_finished`.
- `ResetAll()`: put every element back where the level placed it, for a restart.
- **Blocked changes** (this replaces the old push-out and crush rule): before a view switch, `CanSwitchTo(view)` places everything where the new view would put it, checks that no moving element (player, crate) is inside something solid, and puts everything back. If one is, the switch is refused and both the element and the obstacle blink for half a second.
- **Turns:** `PrepareRotation(carrier)` tries the carrier's turn the same way and refuses it with the same blink. If it is allowed and the view is flat, the carrier's pieces and the riders standing on it are lifted to their real depth, because a turn always happens in the real layout. `FinishRotation(carrier)` slides them back to the front edge afterwards.
- **How the check works:** tracked pieces are compared box against box from their current transforms, because the physics engine only sees a moved body on the next frame. Everything else, such as the board and its edges, is asked from the physics engine, with the tracked pieces left out.
- **Board edges** are not part of this module. They are colliders on their own layer (`LAYER_BOARD_BOUNDS`) that the player only collides with in top view.

### LevelController

Knows which levels exist, loads them and keeps the player's progress.

| | |
|---|---|
| **Owns** | the list of game levels (the scenes in `scenes/levels/game`, in order of name), the list of test levels (`scenes/levels`), the progress saved in `user://progress.cfg` |
| **Receives** | `LoadGameLevel(index)`, `LoadTestLevel(number)`, `CompleteCurrent()`; queries `LevelCount()`, `UnlockedCount()`, `CurrentLevel()`, `NextToPlay()`, `LevelName(index)` |
| **Emits** | nothing; the GameManager connects the level after each load (`AttachLevel`) |
| **Never does** | run gameplay rules |

Loading frees the old level at once, before the new one is added, so the old scripts let go of their nodes before the new ones start. Progress is one number, how many levels are open: finishing level `n` opens `n + 1`.

### UIController

| | |
|---|---|
| **Owns** | HUD (view indicator, key hint), the main menu, the level selector, the pause menu, the level-complete message, the fade |
| **Receives** | `ShowView(view)`, `ShowComplete(shown)`, `FadeTo(alpha, seconds)`, `ShowMainMenu(level_count, finished_all)`, `ShowLevelSelect(level_count, open_count)`, `ShowPause(shown)`, `HideMenus()` |
| **Emits** | `play_pressed`, `select_pressed`, `back_pressed`, `level_chosen(index)`, `resume_pressed`, `restart_pressed`, `menu_pressed`, `quit_pressed`, all deferred to the end of the frame |

The menus are nodes under `UI/Menus` in `game.tscn`, with one flat theme; restyle them there. The selector's buttons are made in code, one per level.

### BackgroundController

Owns the neon pyramids floating behind every level (`scripts/BackgroundController.cpp`, node `Background` in `game.tscn`). It draws them all as one MultiMesh of white line pyramids and places them every frame across what the active camera sees, far behind the level, so any level size is filled. The GameManager tells it the view with `ShowView(view)` (blended like the camera) or `SnapView(view)` (at once, when a level starts). Settings: `pyramid_count` (160), `drift_speed`, `brightness` (above the glow threshold, so only the lines glow).

### AudioController

| | |
|---|---|
| **Owns** | one `AudioStreamPlayer` per sound, created in `OnReady` from `res://audio/<name>.wav` |
| **Receives** | `Play(sound)`, with a `SoundId` from `GameTypes.h`; the pitch varies a little each time |
| **Emits** | nothing |

It adds itself to the `audio` group. Any script plays a sound with `PlaySound(node, SOUND_...)` from `ElementHelpers.h`, which finds the module through that group and stays silent if there is none. The sounds are synthesized by a script, so they can be replaced by real files with the same names at any time. `sound_volume` on the node shifts all of them, in decibels.

---

## 3. Level elements

Elements are not controllers. Each is a small scene with its own script, placed in a level.

**Contract every element follows**

Each part is optional; an element only writes the ones it needs.

| Part | Purpose |
|---|---|
| `depth` group | The DepthController flattens and restores it. Element scenes carry the group on their root |
| `depth` metadata | Its depth in units before scaling, 1 if absent. The node's Z scale multiplies it |
| `OnViewChanged(view)` | Called by the GameManager on every view change |
| `Reset()` | Called by the GameManager when the level restarts |

Elements must be direct children of the level root; that is where the GameManager and the DepthController look.

**Per-element state lives in node metadata**

Plain variables in a Jenova script are shared by every node that uses it, and a level can hold several plates, doors or turntables. Internal properties looked like the answer, but they get mixed up between nodes when one element calls another through a chain (see section 8). So:

- Per-element state is stored on the node itself with `set_meta` and read back with `get_meta`, for example `door->set_meta("open", true)`.
- Inspector settings stay as `JENOVA_PROPERTY`, but are copied onto the node in `OnReady`, the one place where Jenova always reads them for the right node. Every other function reads the copy.
- Every function takes `Caller* instance` to reach its own node.

**Wiring between elements**

- An activator (plate) has a `target` property, a node path picked in the inspector.
- A receiver (door) exposes `Activate()` and `Deactivate()`.
- The activator calls those on its target when its state changes. One activator drives one receiver for now.

**Events that do reach the GameManager**

- An exit emits `reached`; the GameManager connects to every level element that has that signal.
- The player emits `died`.

**Collision layers**

| Layer | Value | Used by |
|---|---|---|
| `LAYER_WORLD` | 1 | solid things; the player is on it too |
| `LAYER_BOARD_BOUNDS` | 2 | the edges of the board, felt only in top view |
| `LAYER_ACTORS` | 4 | things that press plates and reach exits; the player |

**Built so far**

| Scene | Root | Script |
|---|---|---|
| `block.tscn` | StaticBody3D | none |
| `plate.tscn` | Area3D | `elements/PressurePlate.cpp` |
| `door.tscn` | StaticBody3D | `elements/Door.cpp` |
| `exit.tscn` | Area3D | `elements/Exit.cpp` |
| `crate.tscn` | CharacterBody3D | `elements/Crate.cpp`, a thin script over the shared body in `shared/CrateBody.h` |
| `robot.tscn` | CharacterBody3D, groups `crate`, `depth`, `robot` (not `pushable`) | `elements/Robot.cpp`: the same crate body plus a motor that drives it along the view's line and turns it around when it is stopped |
| `spikes.tscn` | Area3D, with a solid StaticBody3D child | `elements/Hazard.cpp`, `on_board` off |
| `board_spikes.tscn` | Area3D | `elements/Hazard.cpp`, `on_board` on |
| `board_hole.tscn` | Node3D, groups `board_hole` and `swap_fixed` | `elements/BoardHole.cpp`: one cell of void; the player and crates test the holes themselves with `VoidAt` and `FootprintOverVoid` in `ElementHelpers.h` |
| `water.tscn` | Area3D | `elements/Water.cpp` |
| `river_source.tscn` | Node3D, on the board face | `elements/RiverSource.cpp`: lays out its own `water.tscn` stretches at start and forwards the contract calls to them |
| `lever.tscn` | Area3D | `elements/Lever.cpp` |
| `wall_button.tscn` | Area3D | `elements/WallButton.cpp` |
| `view_block.tscn` | StaticBody3D, group `depth`, meta `solid_view` | `elements/ViewBlock.cpp`; the DepthController judges it by the view being checked (`checked_view`), not by its collision switch |
| `board_plate.tscn` | Node3D on the board, group `swap_fixed` | `elements/BoardPlate.cpp` |
| `depth_lift.tscn` | StaticBody3D, groups `depth` and `depth_lift` | `elements/DepthLift.cpp`: same signals and calls as a turntable plus `PendingShift()`; the DepthController checks it with `PrepareShift` |
| `fan.tscn` | Node3D on the board, group `swap_fixed`; its `Wall` child is in group `view_solid` | `elements/Fan.cpp`; the wall of air carries `solid_view` (side, or -1 when off) and the DepthController judges untracked `view_solid` pieces by box |
| `floor_swap.tscn` | Node3D on the hinge, group `floor_swap` | `elements/FloorSwap.cpp`: same signals and calls as a turntable; the DepthController checks it with `PrepareSwap` |
| `turntable.tscn` | Node3D, `carrier` group | `elements/Turntable.cpp` |

**More contract functions**

| Function | Called when |
|---|---|
| `SetFrozen(frozen)` | the world pauses or resumes: during a view slide, a turn, a death, after the level is finished |
| `OnInteract()` | the player presses E; a lever checks that the player is touching it, a wall button ignores depth in side and top |
| `SetCurrent(x, y, z)` | a water segment carries a body this frame; the player and crates have it |
| `Push(x, y, z)` | the player pushes a crate this frame |
| `Kill()` | a hazard touches the player |
| `Break()` | a crate is crushed, falls out or sinks into a board hole |

**Carriers**

A node in the `carrier` group (the turntable) holds level pieces as its children. The GameManager and the DepthController look inside carriers, so those pieces are elements like any other.

**Elements that change the level**

A carrier that turns (a turntable, or a whole ground or board layer) asks first with `rotation_requested`. The GameManager has the DepthController try the turn: if something would end up inside a wall it is refused and the obstacle blinks; otherwise the world freezes, the carrier turns, and `busy_finished` releases it. A request made while the world is paused waits until it is free.

| Carrier | Turns around | Carries riders |
|---|---|---|
| `turntable.tscn` | vertical axis | always |
| `ground_layer.tscn` | vertical axis, the whole ground | always |
| `board_layer.tscn` | depth axis, the whole board | only in top view, where the board is the floor |

A lever flips a carrier between 0 and 90 degrees. Layers that turn must be square, or they stick out of the level after a quarter turn.

**Groups**

| Group | Members |
|---|---|
| `depth` | everything the DepthController flattens |
| `player` | the player |
| `crate` | crates, so rows of crates and the river can find them |
| `pushable` | what the player can push |
| `carrier` | turntables |

---

## 4. Key sequences

### Switching view

1. InputController emits `view_requested(view)`.
2. GameManager checks it is allowed: state is `Playing`, the view differs, no view-lock zone forbids it.
3. GameManager sets state to `Switching` and freezes the player.
4. GameManager calls `DepthController.Flatten()` (going to side or top from iso) or `Restore()` (going to iso). Side ↔ top needs neither.
5. If the player was crushed, go to the death sequence and stop here.
6. GameManager calls `PlayerController.SetMode(view)` and `OnViewChanged(view)` on every element.
7. GameManager calls `CameraController.TransitionTo(view)` and `UIController.ShowView(view)`.
8. On `transition_finished`, GameManager unfreezes the player and sets state to `Playing`.

The world is frozen for the length of the transition (about 0.3 s), so a mid-air switch resumes with the same velocity.

### Death and respawn

1. `PlayerController.died` or `DepthController.element_crushed` reaches the GameManager.
2. State becomes `Dead`; UI plays the death fade.
3. `LevelController.Reload()` resets every element; `DepthController.Clear()` forgets stored depths.
4. Player respawns at the checkpoint in the view the level starts in; state returns to `Playing`.

### Level complete

1. `LevelController.exit_reached` reaches the GameManager.
2. State becomes `Complete`; UI shows the complete screen.
3. On `next_level_pressed`, `LevelController.LoadNext()`.

---

## 5. Scene tree

```
Game                      (Node)             GameManager
├─ InputController        (Node)
├─ CameraController       (Node3D)
│  ├─ Camera              (Camera3D, orthographic)
│  ├─ SidePose / TopPose / IsoPose   (Marker3D)
│  └─ WorldEnvironment
├─ DepthController        (Node)
├─ LevelController        (Node)
│  └─ <current level>     (instanced scene)
│     ├─ Blocks
│     ├─ Elements
│     ├─ PlayerSpawn      (Marker3D)
│     └─ Player           (CharacterBody3D)  PlayerController
├─ UIController           (CanvasLayer)
└─ AudioController        (Node)             plays the sounds
```

`Game` is the project's main scene and stays loaded; only the level under LevelController is swapped.

---

## 6. Files

```
scenes/
  game.tscn
  player.tscn
  levels/level_01.tscn ...
  elements/block.tscn, crate.tscn, plate.tscn, door.tscn, spikes.tscn, exit.tscn
scripts/
  GameManager.cpp
  InputController.cpp
  PlayerController.cpp
  CameraController.cpp
  DepthController.cpp
  LevelController.cpp
  UIController.cpp
  AudioController.cpp
  shared/GameTypes.h        view, death, layer and sound enums
  shared/ElementHelpers.h   shared helpers: bodies in an area, drop shadow, world box of a shape, PlaySound
  shared/CrateBody.h        the crate body shared by crates and robots: falling, rivers, fans, holes, rows of crates, riders
audio/                      the sound effects, one .wav per SoundId
  elements/Crate.cpp, Plate.cpp, Door.cpp, Spikes.cpp, Exit.cpp
```

---

## 7. Build order

Each step is testable on its own.

| # | Build | Maps to PLAN milestone |
|---|---|---|
| 1 | `GameTypes.h`, GameManager skeleton, InputController | 0 |
| 2 | CameraController with three poses and background colours | 1 |
| 3 | PlayerController with three modes, switched through the GameManager | 2 |
| 4 | DepthController: flatten, restore, overlap push and crush | 3 |
| 5 | LevelController, exit, death and respawn, first level | 4 |
| 6 | Element contract, then spikes, crate, plate, door | 5 |
| 7 | UIController, level flow | 6 |

---

## 8. Jenova: what is confirmed and what is not

**Confirmed by building and running `PlayerController.cpp`**

- The project builds with the Meteora backend. Routines available: `OnAwake`, `OnDestroy`, `OnReady`, `OnProcess`, `OnPhysicsProcess`, `OnInput`.
- Every routine can take `Caller* instance` as its first parameter; `GetSelf<T>(instance)` gives the node. Use this in every script.
- Do **not** store the node in a `self` property and cast it later, as the built-in template does. For a `CharacterBody3D` that cast returns null.
- `OnPhysicsProcess(Caller* instance, double _delta)` receives the time step as a plain `double`.
- `JENOVA_PROPERTY` names become global macros placed before the Godot headers. Avoid generic names (`gravity`, `position`, `name`) that could collide with identifiers inside those headers.
- Plain functions in a script (`SetMode`) are exposed as methods on the node, and another script calls them with `node->call("SetMode", value)`.
- Shared headers work when included by their path from the project root: `#include "scripts/shared/GameTypes.h"`. A path relative to the script does not.

**Rules for calls between scripts (Meteora backend)**

| | Works | Does not work |
|---|---|---|
| Parameters | `int`, `double`, several of them, a single `Variant` | `bool` (the call fails), `Vector2` by value (arrives corrupted), two `Variant`s |
| Return values | `bool`, `double`, `Vector2`, `Variant` | `int` (arrives corrupted) |

So flags are passed as `int` 0/1 and vectors as separate `double`s, as `PlayerController::SetMoveInput(double, double, int, int)` does.

**Rules for code inside a script block**

- Global variables must be simple types (numbers, `bool`, Godot math types, pointers). A `std::vector` of a custom struct made the build fail at the metadata step; such data goes outside the block as a file-level `static`.
- Function parameters must not be custom structs for the same reason.
- Read properties into plain globals in `OnReady` when they are used inside functions other modules call.

**Variables: shared or per node**

Confirmed with a probe of two nodes sharing one script:

| Kind | Declared as | Behaviour |
|---|---|---|
| Plain variable | `int counter = 0;` | one value shared by every node using the script |
| Internal property | `JENOVA_PROPERTY(int, counter, 0, Usage:PROPERTY_USAGE_NO_EDITOR)` | one value per node, hidden from the inspector |
| Editor property | `JENOVA_PROPERTY(int, counter, 0)` | one value per node, shown in the inspector |

In that simple probe internal properties kept their own value per node. In the real game they did not: when the GameManager called a lever, which called a turntable, the values written by one turntable ended up in the other, including its axis setting. Internal properties are only reliable inside the engine's own routines, so element state moved to node metadata (section 3). Plain variables remain fine for the controllers, which exist once.

After adding or removing properties, a full rebuild (`RebuildProject`, or Clear Cache Database in the Jenova menu) is needed; otherwise every frame prints "Failed to Update Interpreter Property Storage Value".

**Functions called from other scripts**

- A function reached through `call()` can take `Caller* instance` as its first parameter and gets its own node, the same as the engine routines. The caller does not pass it.
- `NodePath` and `bool` properties work when their value is set on the node in the scene.

**Depth lock and teleports**

- A body with its Z axis locked (`set_axis_lock`) ignores any code that moves it in depth: the node moves, but its physics shape stays behind. Even lifting the lock for the moment of the move does not help, because the lock is applied later in the frame.
- The player and crates therefore use no axis lock. In side and top they keep their depth in code: no depth speed, and any depth drift after a move is put back.

**Signals emitted from inside element routines**

- A signal that makes the GameManager call back into the same element (a death, the start or end of a turn) is emitted with `call_deferred`, so the element's routine finishes first.

**Iterating bodies an area touches**

- Turning each entry of `get_overlapping_bodies()` into a node with `Object* object = bodies[i];` and a plain cast works. `ElementHelpers.h` wraps it as `FirstBodyInGroup` and `HasPlayerInside`.

**Areas after a teleport**

- An `Area3D` still lists a body as overlapping in the frame after that body was moved away by code. Plates and exits ignore contacts for two frames after `Reset()` for this reason.

**Property defaults**

- Number defaults work. A `Color` default is parsed but arrives as black at runtime, so colour properties must be given a value on the node in the scene. The three background colours of the CameraController are set that way in `game.tscn`.

**Signals**

- `JENOVA_SIGNAL(name)` does not register a signal in this version.
- Declare signals in `OnReady` with `add_user_signal("name")`, emit with `emit_signal("name", value)`, and connect with `Callable(node, "FunctionName")`. This is how `view_requested` and `transition_finished` work.

**Building without the editor UI**

`JenovaEditorPlugin.GetInstance().BuildProject()` can be called from a script run with `--headless --editor`, which is how the scripts were compiled and tested. Build errors are not printed to the console; they are in the `JenovaTerminalControl/TerminalLog` node of the editor UI, the same text as the Jenova Terminal panel.
