# Camundrum — Game Plan

A 3D puzzle platformer in Godot 4.7 with C++ scripts through the Jenova Framework.
One character, one level, three ways of reading it: **side**, **top** and **iso**.

Nothing in this plan is built yet except an empty test scene (`main.tscn`).

---

## 1. Rules of the game

### Coordinates

| Axis | Meaning |
|---|---|
| X | left / right |
| Y | up (in side and iso) |
| Z | depth. `z = 0` is the board (the background); higher Z is closer to the viewer |

### The three views

| | Side | Top | Iso |
|---|---|---|---|
| Gravity | down (−Y) | none | down (−Y) |
| Player moves in | X, jumps in Y | X and Y, freely | X and Z, jumps in Y |
| Depth | flattened to `z = 0` | flattened to `z = 0` | real |
| A block is | a platform | a wall | a solid object you can walk around |

- The player can switch views at any time, as often as they like, including mid-air.
- Movement made in one view is kept in the others: walking "up" in top view really changes Y.

### Flattening and remembered depth

- On entering side or top, **every** element (player, crates, blocks, everything) is pushed to `z = 0` and remembers the Z it had.
- On returning to iso, every element goes back to its remembered Z. X and Y stay as they are now.
- While flattened, things that sit at different depths touch each other.

### Overlap after a switch

A switch can leave the player inside something (flattening onto a wall, or restoring depth into a block).

1. Look for the nearest free spot.
2. If one exists within the push limit, move the player there.
3. If the solid is too thick and the player is in the middle of it (no free spot within the limit), the player dies.

The push limit is one tunable number. Starting value: **1 cell**.

---

## 2. Element catalog

Every element answers the same four questions: what it does in side, in top, in iso, and whether it is affected by gravity. New elements get a new row here first, then a scene and a script.

### Tier 1 — needed for the first playable level

| Element | Side | Top | Iso | Notes |
|---|---|---|---|---|
| **Solid block** | platform / wall | wall | solid, has real depth | Depth can be thin (1 cell) or full. The basic building piece |
| **Player** | run, jump, falls | walks freely, no falling | run in X/Z, jump, falls | Dies on hazards, on falling out, on a failed overlap push |
| **Exit** | touch to finish | touch to finish | touch to finish | May be locked by a door in front of it |
| **Spikes** | kill on touch | kill on touch | kill on touch | In top they are just something to walk around |
| **Crate** | falls; pushed left/right | no falling; pushed in X and Y | falls; pushed in X and Z | Remembers its Z like the player. Top view can "lift" it for free |
| **Pressure plate** | pressed by weight | never pressed | pressed by weight | Needs gravity, so side or iso is required. Player or crate can press it |
| **Door / gate** | solid until opened | solid until opened | solid until opened | Opened by a plate, lever or key |

### Tier 2 — more puzzle variety

| Element | Side | Top | Iso | Notes |
|---|---|---|---|---|
| **Lever** | toggle on interact | toggle on interact | toggle on interact | Stays in its state, unlike a plate. A view that would hide it behind a fixed solid is refused |
| **Floor swap** | swaps | swaps | swaps | Driven by a lever, button or plate. Mirrors the level across the slanted plane through the hinge (where the ground meets the board): height and depth trade places, so what stood on the ground sticks out of the board and what was on the board lies on the ground. Pieces fold over on quarter circles in 0.7 s. Refused like a turn if something would end up inside something. Test level `world_swap_test.tscn` (8) |
| **Void (board holes)** | only seen | takes what has nothing under it | only seen | Real openings in the board: each `board_hole.tscn` is one cell, and the board is rebuilt from one-unit tiles without those cells when the level loads (`PrepareLevel` in `LevelController.cpp`). In top the board is the floor of every crate and the player: a crate falls only when its whole base is over open holes, so a long crate bridges a small hole, and it sinks into the open hole nearest to its middle and plugs only that one cell (it slides onto that cell while it sinks); the player falls when its centre is over an open hole. This is checked all the time, also right after a view switch. In side and iso the holes are only seen, the wall stays solid. In the editor a hole still shows as a dark marker |
| **View block** | solid only if `solid_view = 0` | solid only if `solid_view = 1` | solid only if `solid_view = 2` | `view_block.tscn`. Opaque in its own view, see-through in its view's colour and passable in the other two. A switch that would leave the player or a crate inside one that becomes solid is refused |
| **Flag (exit)** | counts if `goal_view` is -1 or 0 | counts if -1 or 1 | counts if -1 or 2 | `exit.tscn`, now a flag. `goal_view = -1` is a warm white flag for any view; otherwise it takes its view's colour and only counts there, see-through elsewhere. Only the player reaches it, not crates |
| **Board plate** | up | pressed by a crate covering half of it or the player's centre | up | `board_plate.tscn`, one cell on the board. Leaving top lets whatever lay on it fall, so it comes up again, unless `stays_pressed` is on |
| **Depth lift** | E on it: next stop, moved in depth unseen; only the arrival has to be free | same as side | E on it: slides there with its riders; anything in the way stops it | `depth_lift.tscn`. `stop_count` stops, `stop_step` apart, going deeper towards the board from where it is placed (stop 0). A digital display on its front shows the stop. In every view the layout is judged as in iso |
| **Fan** | touching its column blows you off the level: tossed up a little, in front of everything, you fall through it all out of sight and die when out of the level, a crate is lost the same way; a switch to side that would leave something inside the column is refused | a jump seen from above: standing on it raises the player or a crate, which can then go over anything (walls, doors, crates); it stays up while over something solid; after the fan's jump it also stays up 0.3 s in the air, so it can reach a wall nearby or jump a short gap, but walking off a wall brings it straight down. Up there it comes forward towards the camera with its shadow slipping off to the side. Its depth never changes | blows the player and crates away from the board, up to the first solid piece in front | `fan.tscn`, on the board's face like a hole, group `fan`. Leaving top while up on something: to side it falls into the void the same visible way (`DropIntoVoid`); to iso it appears against the front face of what it stood on (`PlaceRaisedBodies` in the DepthController). Raised bodies use collision layer `LAYER_RAISED` and only meet the board's edges and each other. `wind_speed` (7), `starts_on`; a lever, button or plate switches it. Test level `y_fan_test.tscn` (10) |
| **Crate chains** | push along | push along | push along | A crate the player pushes moves together with every crate lined up touching in front of it, as one block in the same frame (`MoveRow` in `CrateBody.h`): each crate is tested against the world with the rest of the row left out, and the row goes as far as its most blocked crate allows. Only a push from the side counts in side and iso; standing on a crate does not push it. Crates also come 2 and 3 tall (`crate_tall2.tscn`, `crate_tall3.tscn`); stairs puzzle in `x_crate_stairs.tscn` (9) |
| **Robot** | drives left and right, with gravity | drives up and down, no gravity | falls and keeps the line of the last flat view: depth after top, left and right after side (depth if it never left iso) | `robot.tscn`, a grey crate with a red antenna that blinks while on, group `robot`. Off at the start (`starts_on`); E next to it switches it on and off, and a lever, button or plate can too. Bumping into a wall, a blocked row of crates, another robot or the player sends it back the other way. It pushes crates, carries the player and crates standing on it, and cannot be pushed. Two senses are kept: left/right, and one shared by top's up/down and iso's depth (up in top = towards the camera in iso). `starts_forward` picks right/up/towards the camera, `move_speed` (2). Otherwise it is a crate: plates, holes, void, rivers and fans treat it the same (`CrateBody.h`). Example level `game/09_z_robot_test.tscn` ("el ascensor de cristal": steer the robot through top, iso and side onto a glass ledge) |
| **Wall button** | toggle on interact, depth ignored | toggle on interact, depth ignored | toggle when standing in front of it | Mounted on the board or as a child of a block or door, its local +Y pointing out of the face. Always in sight, so it can be pressed from the front edge. Test level `wall_button_test.tscn` (7) |
| **Key and lock** | pick up, open lock | pick up, open lock | pick up, open lock | |
| **Hole in the board** | nothing, just background | the player falls in and dies | visible gap behind the level | Gives top view its own hazard |
| **View-lock zone** | — | — | — | An area where one or two views are disabled. Strong puzzle tool |
| **Phase block** | solid | not solid | not solid | Solid in only one view (any of the three). Colour-coded |
| **Moving platform** | carries the player | a moving wall that pushes | carries the player | Follows a fixed path |
| **One-way platform** | jump up through it | passable from one side only | jump up through it | |
| **Spring** | launches upward | no effect | launches upward | |
| **Checkpoint** | sets respawn | sets respawn | sets respawn | Also stores remembered Z |

### Tier 3 — later, if the game needs them

Patrolling enemies, conveyors, ice, collectibles, timed doors, lasers that only block at their real depth.

---

## 3. How it is built

### Level layout

- Levels sit on a **1-unit grid**. Blocks are placed on cells; the player and crates move smoothly.
- One scene per level. Elements are reusable scenes dropped into it.

### Core systems

| System | Job |
|---|---|
| **View manager** | Holds the current view, handles the switch input, tells everything else when the view changes |
| **Camera rig** | Three cameras (side and top orthographic, iso angled) with a short animated transition |
| **Flatten system** | On a switch, stores or restores Z for every element, then runs the overlap check |
| **Player controller** | Three movement modes, picked by the current view |
| **Element base behaviour** | The shared part of every element: react to a view change, remember Z, optional gravity |
| **Level manager** | Death, respawn, restart, finish, load next level |

### Default controls

| Input | Action |
|---|---|
| A / D | move left / right |
| W / S | top: move up / down. Iso: move in depth |
| Space | jump (side and iso) |
| 1 / 2 / 3 | side / top / iso |
| E | interact |
| R | restart level |

### Jenova notes

- Scripts are C++ files attached to nodes, using `OnAwake`, `OnReady`, `OnProcess` and `JENOVA_PROPERTY`, built from inside the Godot editor.
- Godot currently reports "Module Cache Cannot Be Found" and "Bindings Database Failed to Load". The project has never been built; milestone 0 clears this.
- No Jenova script has been compiled in this project yet, so how scripts talk to each other (signals, groups, direct calls) is confirmed in milestone 0 before anything depends on it.

---

## 4. Milestones

| # | Goal | Done when |
|---|---|---|
| 0 | **Pipeline** | One C++ script builds and runs (the test cube spins). Jenova warnings gone. Two scripts can signal each other |
| 1 | **Views** | Three cameras, switching with 1/2/3, animated transition, a test room of blocks |
| 2 | **Player** | Correct movement in each view; switching mid-air behaves |
| 3 | **Flattening** | Z is stored and restored for everything; overlap push and death work |
| 4 | **First level** | "Around, Up, Drop" is playable start to finish with exit, death, restart |
| 5 | **Tier 1 elements** | Spikes, crate, plate and door work in all three views |
| 6 | **Game flow** | Level select, progress saved, 5–8 levels teaching one idea each |
| 7 | **Tier 2 elements** | Added one at a time, each with a level that needs it |
| 8 | **Polish** | Art, sound, transitions, menus |

---

## 5. Decisions

**Settled**

1. **Crates and overlap.** Crates follow the player's rule: pushed to the nearest free spot, destroyed if too deep inside a solid.
2. **Telling the views apart.** The side camera has a slight tilt. Each view has its own background colour (the sky behind the level, not the board).

**Still open**

3. **Hole in the board.** Proposed as top view's own hazard; not part of the original idea.
4. **Pressure plates needing gravity.** Also my addition. Without something like it, top view can do everything side view can.

The module design is in [ARCHITECTURE.md](ARCHITECTURE.md).

---

## 6. Where the project is

**Built and tested**

| Piece | State |
|---|---|
| Player, input, camera, depth and game manager modules | working |
| Three views with view-dependent physics | working |
| Remembered depth with an animated slide, push-out and crush | working |
| First level geometry: board, floor, column, ledge | working |
| Movement feel: acceleration, coyote time, jump buffer, variable jump, faster fall | working |
| Queued view switch and background colour per view | working |
| Reusable block, pressure plate, door and exit | working |
| First level playable from start to exit | working |
| Elements: crates (rows, tall), rivers, fans, holes, view blocks and flags, floor swap, depth lift, board plate, wall button, turntable, robot | working |
| Six game levels, unlocked in order, with saved progress | working |
| Top seen from above (turned pieces, light), neon pyramid background, colour per view | working |
| Animated menus, options (volumes, screen shake, fullscreen) saved in `user://settings.cfg` | working |
| View bar hop on a switch, level title card on start | working |

**Decided**

- Levels are built in the Godot editor from reusable element scenes, placed with grid snap at 1 unit. No level-design plugin for now.
- In flat views every element is pushed to the front edge of the level, not against the board.
- Controls in iso follow the world axes, the same as in side view. Camera-relative controls are an open option.

---

## 7. Roadmap

Each block is finished when its "done when" line is true in the running game.

### Block 1 - Movement feel (done)

| Item | Setting on the Player node |
|---|---|
| Acceleration and braking | `ground_acceleration`, `ground_deceleration`, `air_acceleration` |
| Coyote time | `coyote_time` |
| Jump buffer | `jump_buffer_time` |
| Variable jump height | `jump_cut` |
| Faster fall with a speed cap | `rise_gravity`, `fall_gravity`, `max_fall_speed` |

### Block 2 - Finish the first level (done)

| Item | How it ended up |
|---|---|
| Element contract | Optional functions `OnViewChanged(view)` and `Reset()`; the GameManager calls them on every level element that has them |
| Reusable block scene | `scenes/elements/block.tscn`, a 1-unit cube. Scale the node to size it; its depth is read from the scale |
| Exit | `exit.tscn`. Touching it finishes the level |
| Pressure plate | `plate.tscn`. Pressed by an actor standing on it, never in top view. Its `target` is picked in the inspector |
| Door | `door.tscn`. Opened and closed by its activator; `stays_open` keeps it open after the first press |
| Level complete | A placeholder message; R plays again |

Done when: the "Around, Up, Drop" level can be completed from start to exit. Confirmed in a full simulated playthrough.

**Building a level in the editor**

1. Drag element scenes from `scenes/elements` into the level, as direct children of its root.
2. Turn on grid snap at 0.5 or 1 unit and move them into place.
3. Scale a block to make it wider, taller or deeper.
4. For a plate, pick the element it drives in its `target` property.
5. Set `flat_front_z` on the level root to the Z of the level's front edge.

### Block 3 - View switch feel

| Item | Notes |
|---|---|
| Queued switch | done |
| Background colour per view | done |
| Death and crush feedback | Flash, camera shake, short fade before the restart |
| Sound on a switch | done, with a small zoom punch of the camera; a refused switch buzzes and knocks the camera |

Done when: a death is understandable without reading any text.

### Block 4 - Reading the space (done)

| Item | Notes |
|---|---|
| Shadow under the player in iso | Shows depth and where a fall will land |
| Hint of remembered depth in side and top | removed: the see-through look was more confusing than helpful |
| Active view indicator | On screen |

Done when: a new player can tell which view is active and how deep they are.

### Block 5 - More elements and levels

| Item | Notes |
|---|---|
| Level controller | Loads levels, tracks the checkpoint |
| Crate | Pushable, falls, remembers its depth |
| Spikes | Kill on touch |
| 5 to 8 levels | One idea each |

Done when: several levels can be played in a row.

---

## 7b. Making the game's levels

The menu plays the levels in `scenes/levels/game/`, in order of file name. Nothing else needs to be registered.

1. In the FileSystem panel, right-click `scenes/level_template.tscn` and choose **Duplicate**. Name it with a number first, such as `03_first_river.tscn`.
2. Drag the copy into `scenes/levels/game/`.
3. Open it and build the puzzle with the scenes in `scenes/elements/`, with grid snap on. The template already has the board, the ground, the board edges, the player, the exit and a light.
4. Press F5 with `Level Number = -1` on the Game node: the menu lists the new level.

- **Order:** the number at the start of the name. To move a level, rename it. Leave gaps (10, 20, 30...) to slot levels in later.
- **Name in the selector:** the file name without the number and with spaces for underscores, so `03_first_river` shows as "first river".
- **Testing one level quickly:** `Level Number` on the Game node still plays a level from `scenes/levels/` straight away, with no menu. Game levels can be tested from the selector.
- **Plates and switches:** a ground plate is only pressed in side and iso (top has no weight) and lets go when you enter top, which switches its target off. If what it drives must stay on, such as a fan you need in top, turn on `stays_pressed` on the plate (ground and board plates both have it) or `stays_open` on a door.
- **Starting view:** add `metadata/starting_view` to the level root (0 side, 1 top, 2 iso); without it the level starts in the view set on the Game node. Mind where the player stands in that view: in side a player placed in the air falls straight away.
- **Never give a piece a negative scale,** and do not put blocks inside other blocks: place each block as its own child of the level root. A negative scale can make collisions misbehave.
- **Bigger levels:** the camera frames each level by itself when it loads (`FrameLevel` in `CameraController.cpp`): it measures the boards and the fixed pieces, centres every view on them and opens the zoom if they do not fit, keeping the angles of the poses in `game.tscn`. A level of the usual size keeps the usual zoom. For more or less room, add `metadata/camera_margin` to the level root (1.15 by default).
- **A bigger board:** never scale the board's `Mesh`; that only makes it look bigger, while the play area stays the same. Instead:
  1. Pick the size in whole units: the **width odd** (13, 15, 17, 19...) so the cells sit centred on whole numbers like every piece, and the **bottom edge on a whole number** (usually -1, the bottom of the ground).
  2. Set that size on the board's two resources: `BoxMesh_board` (what is seen) and `BoxShape3D_board` (what is solid). Leave the depth at 0.5.
  3. Move the `Board` node so it covers that area: x = 0, y = bottom + height / 2, z = -0.75.
  4. Move the `BoardBounds` boxes just outside it, one unit thick: `Left` at x = -(width / 2 + 0.5), `Right` at x = width / 2 + 0.5, `Top` at y = top + 0.5, `Bottom` at y = -1.5; make `BoxShape3D_bound_side` tall enough to cover the board and `BoxShape3D_bound_top` (used by Top and Bottom) wide enough to reach both sides.
  5. The camera frames the bigger level by itself.

  Example, `06_lvl2.tscn`: a 19 × 10 board from y = -1 to 9 sits at (0, 4, -0.75), with Left at x = -10, Right at 10, Top at y = 9.5 and Bottom at -1.5.
- **The Bottom edge:** the template has one under the ground, so in top the player cannot leave through a gap in the ground. Keep it when you make gaps.
- **A deeper ground:** change `metadata/flat_front_z` on the level root to the ground's front edge, and stretch the `BoardBounds` boxes to the same depth, as in `x_crate_stairs.tscn`.
- **Progress:** finishing a level opens the next. To start over, delete `progress.cfg` from the project's user data folder (Project > Open User Data Folder).
- `01_example_push`, `02_example_stairs`, `03_through_the_wall` (view blocks, a depth lift, a flag of iso) and `04_two_floors` (a ground plate and a board plate) are examples; delete them when your own levels are in.

## 7c. Bugs to find

| Reported | What happens | Steps given | What was checked |
|---|---|---|---|
| 2026-10-18 | In `03_through_the_wall`, going back to iso is refused and the iso wall blinks, as if something were inside it, with nothing there | Ride the lift in side to the back stop; iso, behind the wall; climb the board in top; drop in side to go to the front; ask for iso | Not reproduced. Four variants worked: dropping straight down, walking left to the step, riding the lift back to the front, standing pressed against the wall in iso first. The switch is judged with the player at its real depth, which stayed behind the wall (or in front after the lift) in every test. Next time: note what blinks besides the wall, where the player is, and take a screenshot |

## 8. New elements (agreed design)

### Rules

**River**
- A chain of water segments. Each segment flows along its own X axis, so rotating it in the editor sets the direction, at `flow_speed`.
- It carries every dynamic object inside it: the player and boxes.

| View | Current |
|---|---|
| Top | none, the water is still |
| Side | only the left/right and up/down part of the flow |
| Iso | the whole flow, depth included |

**Boxes**

| View | Behaviour |
|---|---|
| Side | fall; pushed left/right |
| Top | no falling; pushed in any direction over the board |
| Iso | fall; pushed left/right and in depth |

- They remember their depth like the player, press plates, and are destroyed if a switch leaves them stuck too deep inside something.

**Hazards on both floors**

| Hazard | Top | Side and iso |
|---|---|---|
| Hole in the board | you fall in and die | harmless, part of the background |
| Spikes on the board | kill | harmless |
| Gap in the ground | harmless | you fall out and die |
| Spikes on the ground | a wall you can't cross | kill |

- A crate whose whole base ends over board holes in top sinks into the nearest of them and plugs that one cell, which becomes floor.

**Turntable**
- A piece of ground that turns a quarter turn, taking everything on it along, the player included.
- Driven by a lever or a plate. It only turns in iso, where depth is real; a request made in another view waits until the next time iso is entered.

**Swap**
- A lever or button that swaps the two floors: the board becomes the ground of side and iso, and the ground becomes the board. Done as the floor swap, see below.

#### Floor swap rules for level design

- Place `floor_swap.tscn` on the hinge line: the height of the ground's top and the depth of the board's face. Scale it on X so its glowing line spans the level.
- The level must be square: the board as tall above the ground as the ground is deep in front of the board. Otherwise the front edge and the board edges stop matching after a swap.
- The board, the ground (`Floor`), the board edges (`BoardBounds`), lights, river sources and anything in the `swap_fixed` group stay where they are.
- The player and crates only move, they keep standing upright; every other piece also turns over.
- Pieces on turntables and layers are moved one by one, so a layer's turn axis does not follow the swap: avoid putting both in the same level.

### Steps

| # | Step | Done when | State |
|---|---|---|---|
| 1 | Death feedback | a death is visible: the player vanishes, the camera shakes, the screen fades before the restart | done |
| 2 | Boxes | the weight puzzle works and a box holds a door open only while it sits on the plate | done, `box_test.tscn` |
| 3 | Hazards on both floors | the four rows of the table behave as written, and a box fills a board hole | done, `hazard_test.tscn` |
| 4 | River | the ferry puzzle works: a box and the player ride the river and fall down a waterfall | done, `river_test.tscn` |
| 5a | Turntable and lever | one level is solved by turning a wall | done, `turntable_test.tscn` |
| 5b | Swap | one level is solved by swapping the floors | waiting on a design decision |
| 6 | Feedback round | see below | done |

Each step has its own small test level in `scenes/levels/`. To play one, set `level_number` on the `Game` node in `game.tscn`. Levels are numbered from 0 in alphabetical order of their file names, and the numbered list is printed to the Output panel when the game starts; -1 plays the level placed in the scene.

**Feedback round (done).**

| Item | Result | Test level |
|---|---|---|
| Blocked switch | A view switch or a turn that would leave the player or a crate inside something is refused, and both blink. Replaces push-out and crush | `box_test.tscn` |
| Readable deaths | Spikes: red flash and a bounce. Hole: the player shrinks away. Fall: vanishes | `hazard_test.tscn` |
| Board holes in iso | Replaced: holes are only seen in side and iso, see the void row of the catalog | `hazard_test.tscn` |
| Flat platform river | A thin sheet on the ground with faint slow streaks | `river_test.tscn` |
| River source | Placed on the board, it lays out its own river: down the board, straight along the ground towards the camera, over the edge. A fixed block ends it, a crate is carried. In side and top only the stretch on the board exists, since those views have no depth; the ground part and the fall appear and carry only in iso. Switching views plays the change: the water drains off the ground when leaving iso and runs out along it again when entering. Replaces the separate board river and hand-placed segments | `river_test.tscn` |
| Turns in any view | Turntables turn straight away in side and top too | `turntable_test.tscn` |
| Ground and board levers | A lever turns the whole ground or the whole board a quarter turn, back and forth | `rotation_test.tscn` |

**Open decision for the swap.** A rigid turn of the level that puts the board on the ground and the ground at the back necessarily mirrors left and right: what was on the left ends on the right. The alternatives are accepting the mirror, or a swap that is not a rigid turn (each piece is moved to its mirrored place but keeps its orientation).
