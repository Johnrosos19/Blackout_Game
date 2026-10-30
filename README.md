# BLACKOUT – "Light is the enemy"  (Group 1)

Reach the green exit. Patrolling bulbs cast real omnidirectional shadows. Standing in light fills the
alarm meter, standing in shadow drains it. Meter full = SPOTTED.

## Build (Visual Studio 2019/2022 Community)
1. Install the **Desktop development with C++** workload.
2. One-time, in a *Developer PowerShell*:  `vcpkg integrate install`
   (VS 2022 ships vcpkg; if `vcpkg` is not found, run it from `C:\Program Files\Microsoft Visual Studio\2022\Community\VC\vcpkg`, or install vcpkg from github.com/microsoft/vcpkg).
3. Open `BLACKOUT.sln`, choose **x64**, press **F5**. `vcpkg.json` makes VS download GLEW, GLFW and GLM automatically on first build.
   (Alternative: open the folder with CMake – `CMakeLists.txt` is included.)
4. The working directory is the project folder, so `Shaders/` and `Scenes/` are found. Running the .exe from `x64\Debug` also works.

Needs a GPU/driver with OpenGL 4.6 (falls back to 4.5 / 4.3 and rewrites the `#version` lines; the console says so – log it in your decision log).
Everything is generated in code (textures, sky, meshes) – no image files needed. Level: `Scenes/level1.txt`; pass another file as argv[1].

## Controls
WASD move · mouse look · **F** torch · **P** pause · **R** restart / next level · **Enter** start · **Esc** quit

Debug / demo ("break it on purpose") keys
| Key | Effect |
|---|---|
| **V** | colour the frame by shadow factor (R,G,B = bulbs 0,1,2) |
| **1** | GPU shadows off – picture says lit everywhere, game still uses real shadows |
| **2** | CPU ignores walls – you get caught while standing in what *looks* like shadow |
| **3** | huge omni bias – shadows detach from objects |
| **0** | fix everything |
| **[ ]** | omni bias −/+ 0.01 (world units) |
| **C** | E1: cache cube maps, re-render only when a bulb moved (HUD shows maps rebuilt n/N, GPU shadow ms) |
| **Y** | vsync on/off (turn off before comparing frame times) |
| **T** | print average ms/frame each second |
| **L** | E2: HUD shows live GPU pixel illumination next to the CPU number |
| **G** | print CPU vs GPU illumination at the player |
| **F9** | **P3 study**: 20 positions (10 within ≤0.09 m of a shadow edge), CPU vs GPU, distance to nearest edge → console + `p3_results.csv` |

## Requirement map
| Spec | Where |
|---|---|
| B1 GL 4.6, `#version 460` everywhere | `main.cpp` window hints, all shaders |
| B2 `glDrawElements` for every shape | `Mesh.cpp` (box = 24 verts, sky cube = 8) |
| B3 resize without stretch | `FramebufferSizeCallback`, aspect used in `glm::perspective` |
| B4 camera + dt movement | orbit camera, `Game::Update` uses dt |
| B5 ≥5 materials | asphalt, concrete, brick, metal, wood, hazard in `level1.txt` |
| B6 ambient/diffuse/specular | `shader.frag` |
| B7 directional shadows | `directional_shadow_map.*`, `DirectionalShadow()` |
| B8 ≥2 shadowed point lights | 4 bulbs, one cube map each (`omni_shadow_map.vert/.geom/.frag`) |
| B9 spot light | torch (F), cone + fade in `shader.frag` |
| B10 skybox | `skybox.*`, procedural night cube map (`Textures.cpp`) |
| B11 level from file | `Scene.cpp`; errors reported as `file:line: message`; missing textures → magenta checker + message |
| B12 HUD | `Hud.cpp`, `glm::ortho`, letter/digit strip texture (no font library) |
| B13 win / lose / restart | `Game.cpp` |
| Mechanic | 2 patrolling bulbs + 2 lamps, ≥4 shadow-casting hiding objects, alarm meter, exit |
| P1 formula | header of `LitTest.h` |
| P3 | F9 |
| E1 + E2 | C key / L and G keys, HUD light meter |

## Game state machine
`TITLE –Enter→ PLAYING` · `PLAYING –P→ PAUSED –P→ PLAYING` · `PLAYING –exit reached→ WON` · `PLAYING –alarm ≥ 1→ LOST` ·
`PLAYING –R→ PLAYING` · `WON –R/Enter→ PLAYING (next level, bulbs ×speedstep, alarm fills faster)` · `LOST –R/Enter→ PLAYING (same level)`

## Texture unit list (main program)
| Unit | Sampler | Type |
|---|---|---|
| 0 | `theTexture` | sampler2D |
| 1 | `directionalShadowMap` | sampler2D |
| 3–6 | `omniShadowMaps[0..3]` | samplerCube |
| 8 | `skybox` (skybox program) | samplerCube |
| 9 | `hudTexture` (HUD program) | sampler2D |

## Design decisions worth writing in your decision log
* **Shared level data**: collision, light blockers, and shadow casters all read the same `box` lines – no second copy of the level.
* **CPU lit-test point** is at floor level under the player (y = 0.01); the GPU probe reads the floor pixel at the same x,z.
* **No N·L term** in the gameplay `illum`; the picture still uses it for shading.
* **The player does not cast into the cube maps** (otherwise standing in light would shadow your own feet and the CPU point-test would disagree by construction). The torch is not counted as "light" for the alarm.
* Only the four bulbs count toward the alarm; moonlight does not.
* E1 cache is exact here because nothing blocking ever moves; a moving blocker inside a bulb's range would make that map stale until flagged dirty.
* The omni shadow pass is **one draw pass per bulb** (`glFramebufferTexture` attaches the whole cube; the geometry shader writes `gl_Layer`), rasterising 6× the triangles.

## Honest notes
* This was written without access to a Windows/OpenGL machine here, so it has not been run on a GPU. I syntax-checked every `.cpp` with stub headers and simulated the level (CPU lit test) in Python: it is winnable at levels 1–4. Expect to tune `level1.txt` (bulb speed/intensity, `alarm` line) after playing it.
* The proof tables (P1–P4), predictions, report, logs and role hand-ins are yours to write; the tools above generate the numbers. Write your P2 prediction **before** pressing F9.
