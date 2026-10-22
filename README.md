# CAMUNDRUM 🎥 - Puzzle C++ Game made in Godot / Jenova Framework

A 3D puzzle platformer about one level read from three cameras: **side**, **top** and **iso**.
Every switch of camera changes the rules of the world. In side the level is a classic platformer, in top the back wall becomes the floor of a top-down game, and in iso the real 3D depth comes back.
The whole game logic is written in C++ through the **Jenova Framework**, which runs native C++ scripts inside Godot 4.7. This project was a deep dive into gameplay architecture, level design and game feel, built around one strong idea: depth is flattened when you look from the side or from above, but the world never forgets it.

<!-- Add a gameplay GIF here: drag the file into a GitHub issue or the README editor and paste the link -->

## 🛠️ Technologies

* Godot Engine 4.7
* Jenova Framework (C++ scripting for Godot)
* C++ / MSVC
* Jolt Physics
* Godot Shading Language
* GitHub

---

## 💡 My Core Contributions

I designed the game and built its systems, levels and feel. My main responsibilities included:

### 1. Three Views, One World (Depth System)
The heart of the game is a depth system that lets the player switch camera at any time, even mid-air.
On entering side or top, every element of the level (player, crates, blocks...) is pushed onto the board and remembers the depth it had. On returning to iso, everything goes back to its remembered depth while keeping where it moved to.
Before any switch, the system lays out the next arrangement and checks it for overlaps. If the switch would trap the player or a crate inside a solid, it is refused with clear feedback instead of breaking the level.

### 2. Player Controller & Game Feel
The player has three movement modes, one per view: platforming with gravity in side, free top-down movement in top, and full 3D movement in iso.
Movement is tuned with acceleration and braking curves, coyote time, jump buffering and variable jump height. On top of that sits a layer of juice: squash and stretch, leaning, dust, screen feedback, sounds, and an x-ray dot pattern that shows the player when a wall hides them.
While the world turns between views, controls are locked so the player never acts on a half-built layout.

### 3. Puzzle Elements
Each mechanic is its own reusable scene and C++ script, following a shared contract (react to a view change, freeze during a switch, reset on restart):
* **Crates** that can be pushed in rows, stacked as stairs, carried by rivers and dropped into holes.
* **Rivers and waterfalls** that rebuild themselves for each view and animate the current being cut and flowing again.
* **Fans** that push you away from the board in iso, blow you into the void in side, and work as a jump button in top that lets you climb onto walls.
* **Real holes in the board** that only matter when something lives on that floor.
* **View blocks and view flags**, solid or counting only in one camera.
* **Floor swaps, depth lifts, turntables, plates, levers, wall buttons and doors**.

### 4. Camera, Levels & Menus
A camera controller animates between the three cameras and frames every level automatically from its size.
A level controller loads levels in order, unlocks them as you progress and saves your progress, while the UI handles the main menu, level select, pause and level-complete screens.
An audio controller plays the game's sound effects on view switches, jumps, landings, plates, doors and deaths.

<!-- Add screenshots of the three views here -->

---

## ⚙️ Game Architecture & Systems

The codebase follows one rule: **one controller, one job**, with the GameManager as the only connector between them.

* **GameManager:** Holds the game state and routes everything. Commands go down as method calls, events come up as signals.
* **InputController:** Reads the keyboard and turns it into game requests (move, jump, switch view, interact, restart, pause).
* **PlayerController:** Movement, death and respawn, with one mode per view.
* **DepthController:** Flattens and restores depth, checks every switch before it happens, and handles special layouts (floor swaps, depth lifts, raised bodies).
* **CameraController:** Three camera poses with animated transitions and automatic level framing.
* **LevelController:** Level lists, loading, progress saving and cutting real holes into the board.
* **UIController & AudioController:** Menus, screens and sound.
* **Level elements:** Wire to each other locally inside each level scene (a plate opening a door never passes through the GameManager).

### Working with Jenova
Jenova compiles C++ scripts attached to Godot nodes from inside the editor. Its rules shaped the code: the state of each element lives in node metadata, scripts talk through signals and groups, and shared helpers live in common headers (`scripts/shared`).
Physics queries are done with box tests on the collision shapes instead of area overlap lists, which proved unreliable for bodies that stand still or are teleported between views.

---

## 🧩 Game Design & Level Flow

Every puzzle is built on reading the same space in three different ways:

| | Side | Top | Iso |
|---|---|---|---|
| Gravity | Down | None | Down |
| The player moves in | Left / right, jumps | Freely across the board | Left / right and depth, jumps |
| Depth | Flattened | Flattened | Real |
| A block is | A platform | A wall | A solid object |

* **Align in one view, use it in another:** Crates of different heights are lined up in iso, then climbed as stairs in side.
* **Walls that are not walls:** A wall that blocks you in top may be a platform you can jump over in side.
* **Remembered depth:** What you move in top keeps its depth, so the iso view can surprise you.
* **Holes and void:** The void is part of the scenery. It only swallows what lives on its floor.
* **Wind and water:** Fans and rivers move you and your crates differently in every camera.

### Content & Level Design
Levels are single scenes built on a 1-unit grid from reusable element scenes, starting from a level template.
They are played in order from the level select and unlock one after another. Each level can choose its starting view and goal flags can belong to one view only, which works as a way to limit how a level is solved.

### 🎮 Controls

| Input | Action |
|---|---|
| A / D | Move left / right |
| W / S | Top: move up / down. Iso: move in depth |
| Space | Jump (side and iso) |
| 1 / 2 / 3 | Switch to side / top / iso |
| E | Interact |
| R | Restart level |
| Esc | Pause |

---

## 🚀 Build Instructions

**Prerequisites:**
* Windows x64
* Godot 4.7 with the [Jenova Framework](https://github.com/Jenova-Framework/J.E.N.O.V.A) (MSVC build)

**Steps:**
1. Clone the repository.
2. Open the project folder with the Jenova build of Godot 4.7.
3. Open Jenova's **Package Manager** and install the packages the project lists: *Microsoft Compiler Bridge (MSVC-Bridge)* and *Godot SDK 4.7.0 (MSVC)*.
4. Click **Jenova → Build** to compile the C++ scripts.
5. Run the project (F5). The main scene is `scenes/game.tscn`.
