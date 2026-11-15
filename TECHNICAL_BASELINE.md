# Technical Baseline: Vita Dungeon Renderer

**Project folder:** `MarySkelterVita`  
**Application:** "Vita Dungeon Test" (title ID `VDNG00001`, version `01.00`; both placeholders)  
**Baseline date:** 2026-11-14  
**Baseline build:** end of phase 5 (`build/VitaDungeonTest.vpk`, built 2026-11-14 16:53)

This document records the state of the project after the five renderer phases. It describes what exists and how it works, and how well each part has been verified. It doesn't propose new work.

All code, textures and art in this project are original. No Mary Skelter assets, code or content are used. See `TECHNICAL_AUDIT.md` for the analysis of the original game and its legal situation. That file is unchanged.

---

## 0. Verification legend

Every claim in this document carries one of these tags:

| Tag | Meaning |
|---|---|
| **[HW]** | Reported working on a real modded PS Vita by the project owner. Reports were made per phase ("works", "performance is good"). |
| **[HW-PHASE]** | Part of a phase that was reported working, but this specific feature or toggle was not individually reported. |
| **[BUILD]** | Compiles and links with the installed VitaSDK. Nothing beyond that. |
| **[SRC]** | Established only by reading project source, SDK headers, or disassembly of the installed SDL2/vitaGL libraries. |
| **[UNVERIFIED]** | Not measured or tested anywhere. |

**No frame-rate, frame-time, RAM or VRAM measurements from the device have been recorded.** The owner reported only that performance is good. All numbers below are derived from source code or build output, and are labelled as such.

---

## 1. Architecture and directory structure

```
MarySkelterVita/
├── CMakeLists.txt            build definition (VitaSDK toolchain, SDL2, VPK packaging)
├── build.sh                  WSL build script (sets VITASDK, passes VITA_MSAA)
├── TECHNICAL_AUDIT.md        audit of the original PC game (unchanged)
├── TECHNICAL_BASELINE.md     this document
├── sce_sys/                  LiveArea metadata (original placeholder art)
│   ├── icon0.png             128x128, 8-bit indexed
│   └── livearea/contents/    bg.png 840x500, startup.png 280x158, template.xml
├── src/
│   ├── main.c                SDL/GL setup, frame loop, debug toggles, HUD content
│   ├── font5x7.h             original 5x7 bitmap font (A-Z, 0-9, + - . : /)
│   ├── engine/               vmath.h (helpers), timing.c/.h (delta time, stats)
│   ├── graphics/             camera, hud2d, level_mesh, texgen
│   ├── gameplay/             room_map, player, collision
│   └── input/                input.c/.h (SDL GameController wrapper)
├── tests/host/               PC-side collision test (never run; see §15)
├── tools/                    make_placeholder_art.py (generates sce_sys PNGs)
├── backups/                  folder copies taken before each phase (5 snapshots)
├── crash-builds/             ELF/SELF/VPK of the build that crashed (immediate mode)
├── assets/                   empty subfolders (audio, models, textures)
├── docs/                     empty
└── build/                    CMake output (not source)
```

**Source size:** 1,999 lines in total across `CMakeLists.txt`, `build.sh`, `src/`, `tests/` and `tools/`. [SRC]

### Module responsibilities [SRC]

| Module | File(s) | Responsibility |
|---|---|---|
| Entry and loop | `main.c` | SDL init, MSAA attribute, GL state, fog setup, shader warm-up, input dispatch, 3D pass, HUD pass, swap |
| Math | `engine/vmath.h` | `Vec3`, degree/radian conversion, clamp, angle wrap |
| Timing | `engine/timing.c` | `SDL_GetPerformanceCounter` delta time, clamped to 1/15 s; FPS, average and maximum over 0.5 s windows |
| Camera | `graphics/camera.c` | `gluPerspective` and view transform on the vitaGL matrix stack |
| HUD | `graphics/hud2d.c` | Batched 2D rectangles and bitmap text through client arrays and `glDrawArrays` |
| Level mesh | `graphics/level_mesh.c` | Builds indexed room geometry from the grid; client-array and VBO draw paths |
| Textures | `graphics/texgen.c` | Procedural 128×128 textures, RGBA8 or RGB565 upload, mipmaps, filter switching |
| Room data | `gameplay/room_map.c` | Hard-coded 12×12 grid, solidity query, cell centres |
| Player | `gameplay/player.c` | Movement relative to yaw, look, D-pad fallback, collision or no-clip |
| Collision | `gameplay/collision.c` | Circle versus grid cells with sub-stepping and push-out |
| Input | `input/input.c` | Controller hot-plug, radial deadzone, button press edges, touch-to-mouse disabled |

### Frame loop order [SRC]
1. `SDL_PollEvent`, then `input_handle_event`
2. `timing_tick`, then `input_poll`
3. Debug toggles (button press edges)
4. `player_update` (movement and collision), then `player_to_camera`
5. 3D pass: depth and cull on, fog per toggle, camera matrices, `glClear(color|depth)`, `level_mesh_draw`
6. HUD pass: `hud2d_begin` (orthographic projection; depth, cull and fog off), text and panels, `hud2d_end`
7. `SDL_GL_SwapWindow`, which calls `vglSwapBuffers`

---

## 2. VitaSDK and library dependencies

| Component | Version / location | Status |
|---|---|---|
| Host build environment | WSL 2, Ubuntu (Windows 11 host) | [BUILD] |
| VitaSDK | `/usr/local/vitasdk`, built 2026-08-25 | [BUILD] |
| Compiler | `arm-vita-eabi-gcc` 15.2.0 (ARMv7-A, NEON, hard-float, Thumb, tuned for Cortex-A9 by default) | [BUILD] |
| CMake | 4.2.3 (project requires ≥ 3.16) | [BUILD] |
| SDL2 | 2.32.8, static, via `find_package(SDL2 CONFIG)` and target `SDL2::SDL2-static` | [BUILD] [HW] |
| vitaGL | `libvitaGL.a`, pulled in through SDL2's link interface | [BUILD] [HW] |
| Other libraries | `vitashark`, `mathneon`, `SceShaccCg_stub`, `SceShaccCgExt`, `taihen_stub` and the `Sce*_stub` libraries, all from SDL2's `INTERFACE_LINK_LIBRARIES` | [BUILD] |
| C++ runtime | `libstdc++`/`libsupc++`, needed because `libvitaGL.a` contains C++ objects; supplied by linking with `g++` (`LINKER_LANGUAGE CXX`) | [BUILD] |
| On-device shader compiler | `libshacccg.suprx`, loaded by vitaGL from `ur0:data/` or `ur0:data/external/` | [SRC] for the path; [HW] in that the app runs, which implies it's present |
| Shader cache | `ux0:data/shader_cache/` (written by vitaGL) | [SRC] |

### Integration facts that shaped the design [SRC]
These come from disassembling the installed libraries.

- **SDL2 sets up vitaGL itself.** It calls `vglInitExtended(0, 960, 544, 0xC00000, msaa)`.
  - The first argument, `legacy_pool_size`, is **0**. Immediate mode (`glBegin`/`glVertex*`/`glEnd`) therefore writes through a NULL pointer and crashes. **[HW]:** this was the cause of crash C2-12828-1, and the build that crashed is kept in `crash-builds/`.
  - Rule: **the project must never use immediate mode.** Every draw uses vertex arrays.
- **MSAA mapping:** SDL maps `SDL_GL_MULTISAMPLESAMPLES` 2 to 2x and 4 to 4x (also 8 and 16 to 4x); any other value means no MSAA.
- **Swap interval:** `SDL_GL_SetSwapInterval` forwards to vitaGL's `eglSwapInterval`.
- **`main()`:** the Vita isn't listed in `SDL_main.h`, so a plain `main()` is used and `SDL2main` isn't linked.

---

## 3. Rendering pipeline

| Stage | Implementation | Status |
|---|---|---|
| Context | SDL window 960×544 with `SDL_WINDOW_OPENGL`; `SDL_GL_CreateContext` | [HW] |
| Viewport | `glViewport(0, 0, 960, 544)` | [HW] |
| Depth | `glClearDepthf(1)`, `glDepthFunc(GL_LEQUAL)`, enabled each frame for the 3D pass | [HW] (phase 1 depth toggle test) |
| Culling | `glFrontFace(GL_CCW)`, `glCullFace(GL_BACK)`, enabled for the 3D pass | [HW] (phase 1 cull toggle test) |
| Fixed-function pipeline | vitaGL's built-in fixed-function emulation; texture environment `GL_MODULATE` (texture × vertex colour) | [HW] |
| 3D draw calls | **3 per frame**, one `glDrawElements` per material: wall, floor, ceiling | [SRC] |
| HUD draw calls | 1 or more `glDrawArrays(GL_TRIANGLES)`; the batch flushes at 12,288 vertices | [SRC] |
| Present | `SDL_GL_SwapWindow`, which calls `vglSwapBuffers` | [HW] |

**Vertex formats [SRC]:**
- **Level:** 24 bytes, interleaved: position (3 floats), texture coordinates (2 floats), RGBA (4 unsigned bytes). Size checked at compile time with `_Static_assert`.
- **HUD:** separate arrays of 2-float positions and 4-float colours.

---

## 4. 3D room and camera

### Room [SRC]
- **Grid:** 12 × 12 cells, defined in `room_map.c`. `#` is wall, `.` is floor, and anything outside the map counts as solid.
- **Size:** cells are 2.0 units, walls 2.5 units high, and the room spans 24 × 24 world units.
- **Obstacles:** one 2×2 pillar block (cells x 4–5, z 3–4) and two single pillars, at (8, 6) and (3, 9).
- **Start:** the player starts at cell (6, 6), facing −Z.

### Mesh
- **Geometry generated [SRC]**, calculated from the map using the same rules as `level_mesh.c`:

| Item | Count |
|---|---|
| Floor cells | 94 |
| Floor quads / ceiling quads / wall quads | 94 / 94 / 56 |
| Total quads | 244 |
| Vertices / indices / triangles | **976 / 1,464 / 488** |
| VBO / IBO size | 23,424 B / 2,928 B |
| Static capacity (worst case) | 3,456 vertices (82,944 B), 5,184 indices (10,368 B) |

- **Hidden faces [SRC]:** wall faces are only generated between a floor cell and a solid neighbour, so faces nobody can see are never created.
- **Winding [HW]:** every quad is counter-clockwise when seen from inside the room. Confirmed in phase 1 with the cull toggle.
- **Lighting [SRC]:** brightness is baked into the vertex colours per face direction: floor 235, ceiling 170, +X 220, −X 170, +Z 195, −Z 150 (out of 255).
- **Texture coordinates [SRC]:** based on world position, one repeat per 2-unit cell. Wall V runs from 0 at the ceiling to 1.25 at the floor.

### Camera [SRC], working on hardware [HW]
- **Projection:** `gluPerspective(60°, 960/544, 0.05, 40.0)`.
- **View:** `glRotatef(-pitch, X)`, then `glRotatef(-yaw, Y)`, then `glTranslatef(-eye)`.
- **Conventions:** right-handed, +Y up; yaw 0 looks down −Z and positive yaw turns left. Pitch is clamped to ±60°.
- **Eye height:** 1.2 units.

---

## 5. Texture formats and memory estimates

| Texture | Pattern (original, procedural) |
|---|---|
| `TEX_WALL` | Offset stone bricks: 4 rows of 32 px, bricks 64 px wide, 3 px mortar, per-brick tint and noise |
| `TEX_FLOOR` | 2×2 flagstones of 64 px with bevelled edges and value noise |
| `TEX_CEILING` | 32 px wooden planks with a sine-based grain and dark seams |

- **Generation [SRC]:** deterministic integer-hash noise at 128×128, tileable, generated at startup. No image decoder is installed in the SDK, and no files are loaded.
- **Upload [SRC]:** `glTexImage2D`, then `GL_REPEAT` wrap, then `glGenerateMipmap`.
- **Filtering [SRC]:** trilinear (`GL_LINEAR_MIPMAP_LINEAR` / `GL_LINEAR`) by default; the nearest option uses `GL_NEAREST` for both.

### Memory estimates [SRC]
These are calculated values, not measurements. vitaGL may add alignment or padding.

| Format | Base level per texture | Including mips (×4/3) | All 3 textures |
|---|---|---|---|
| RGBA8 (default) | 65,536 B | about 87 KB | **about 256 KB** |
| RGB565 | 32,768 B | about 43 KB | **about 128 KB** |

### CPU-side static buffers [SRC] (sizes from `nm` on the build objects)

| Buffer | Size |
|---|---|
| `hud2d` `batch_col` (float RGBA) | 196,608 B |
| `hud2d` `batch_pos` | 98,304 B |
| `level_mesh` `s_verts` | 82,944 B |
| `texgen` `s_pixels` (RGBA8 scratch) | 65,536 B |
| `texgen` `s_pixels565` | 32,768 B |
| `level_mesh` `s_indices` | 10,368 B |
| **Project total** | **about 475 KB** |

The whole ELF has 1.74 MB of code (`.text`), 14 KB of `.data` and 2.76 MB of `.bss`; the `.bss` figure includes SDL2 and vitaGL. [BUILD]

- **565 support [SRC]:** vitaGL's `glTexImage2D` has a `GL_UNSIGNED_SHORT_5_6_5` code path, and the library has a `read_rgb565` pixel reader.
- **565 on hardware [HW-PHASE]:** visual correctness wasn't separately reported.

---

## 6. VBO and client-array paths

| | Client arrays (**default**) | Static VBO + IBO |
|---|---|---|
| Setup | None; data stays in static arrays | `glGenBuffers` ×2, `glBufferData(GL_STATIC_DRAW)` once at startup |
| Pointers | Real addresses | Byte offsets (`offsetof`) into the bound buffer |
| Indices | `&s_indices[first]` | `first * sizeof(GLushort)` offset into the IBO |
| After drawing | — | Both buffers unbound, so the HUD's client pointers aren't read as offsets |
| Failure handling | — | If `glGetError` reports an error after upload, the buffers are deleted and the HUD shows `VBO UNAVAILABLE` |
| Status | [HW] (default since phase 2) | [BUILD] [SRC]; [HW-PHASE] for the CIRCLE toggle |

**Evidence for the VBO path [SRC]:**
- `glVertexPointer` stores the currently bound `vertex_array_unit` together with the pointer, which is standard GL behaviour.
- vitaGL's fixed-function `glDrawArrays` and `glDrawElements` copy vertex data into its per-frame pool (`vgl_reserve_data_pool` and `sceClibMemcpy`). Reusing CPU-side arrays every frame is therefore safe.

**Open decision:** client arrays remain the default because no client-array versus VBO frame-time comparison has been recorded. [UNVERIFIED]

---

## 7. Player movement and controls

### Movement [SRC], working on hardware [HW]
| Parameter | Value |
|---|---|
| Walk speed | 3.0 units/s at full stick |
| Turn speed | 120°/s at full stick |
| Pitch speed | 90°/s at full stick; clamped to ±60° |
| Player radius | 0.3 units |
| Frame time | Capped at 1/15 s (`TIMING_MAX_DT`) |

- **Direction vectors:** forward = (−sin yaw, 0, −cos yaw) and right = (cos yaw, 0, −sin yaw), matching the camera.
- **Combined input:** stick and D-pad movement together is normalised to a length of at most 1.

### Input processing [SRC], working on hardware [HW]
- **Deadzone:** circular, 0.25. That figure comes from SDL's header, which says resting sticks can read up to about 8,000 of 32,767.
- **Rescaling:** movement starts from zero at the deadzone edge. The move stick responds linearly; the look stick uses a curve exponent of 1.6.
- **Direction:** Y axes are inverted so that pushing up gives a positive value.
- **Controller:** hot-plug handled through `SDL_CONTROLLERDEVICEADDED`/`REMOVED`.
- **Touch:** `SDL_HINT_TOUCH_MOUSE_EVENTS = "0"`. The Vita-specific hint can only choose a touchpad, so it can't disable touch-as-mouse.

### Controls (current, unchanged)
| Input | Action | Status |
|---|---|---|
| Left stick | Move / strafe | [HW] |
| Right stick | Turn / look up and down | [HW] |
| D-pad | Walk (up/down), turn (left/right) | [HW-PHASE] |
| START | Exit | [HW] |
| TRIANGLE | Textures on/off | [HW-PHASE] |
| CIRCLE | VBO / client arrays | [HW-PHASE] |
| SQUARE | Trilinear / nearest filtering | [HW-PHASE] |
| CROSS | Collision on/off (no-clip debug) | [HW-PHASE] |
| L | Fog on/off | [HW-PHASE] |
| R | Vsync on/off | [HW-PHASE] |
| SELECT | RGBA8 / RGB565 textures (regenerates them; brief stall from `glFinish`) | [HW-PHASE] |

The face-button names assume SDL's built-in "PSVita Controller" mapping (`y:b0, b:b1, a:b2, x:b3, start:b11`). START is confirmed on hardware; the other face buttons weren't individually reported. [SRC]

---

## 8. Collision system

### Algorithm [SRC], working on hardware [HW]
1. **Sub-stepping:** each move is split into steps of at most `radius / 2` = 0.15 units, with at most 16 steps.
2. **Resolution:** after each step, the circle is pushed out of every overlapping solid cell. The search covers the 3×3 block of cells around the player and repeats up to 3 times.
3. **Push-out direction:** from the cell's closest point to the circle centre. That removes only the into-wall part of the movement, which produces **wall sliding** and smooth rounding of outside corners.
4. **Fallback:** if the centre ever ends up inside a cell, it leaves by the nearest edge.
5. **Map edge:** solid, because `room_is_solid` treats out-of-bounds cells as walls.

### Known limitations
| Limitation | Detail | Status |
|---|---|---|
| Speed bound | The no-tunnelling guarantee holds up to 16 × 0.15 = 2.4 units per frame. The current maximum is 3.0 × 1/15 = 0.2 units per frame. | [SRC] |
| Floor plane only | No height, steps, slopes or ceiling collision | [SRC] |
| Static grid only | No moving objects and no objects smaller than a cell | [SRC] |
| No-clip mode | Clamps to the map rectangle only. You can stand inside pillars, and the view then looks out through back-face-culled walls. Debug use only. | [SRC] |
| Automated test | `tests/host/test_collision.c` covers 5 cases (head-on, slide, convex corner, inner corner, 200,000-frame random walk) but **has never run**: WSL lacks C library headers (`libc6-dev`). | [UNVERIFIED] |

---

## 9. Fog [SRC], [HW-PHASE]
- **Settings:** `glFogi(GL_FOG_MODE, GL_LINEAR)`, start **4.0**, end **18.0**, colour (0.02, 0.02, 0.04), which is the same as the clear colour.
- **When it's set:** the parameters are set once at startup; `GL_FOG` is enabled or disabled each frame by the L toggle (default on).
- **HUD:** `hud2d_begin()` always disables fog.
- **Library support:** vitaGL's `glFogi` handles `GL_EXP` (0x0800) and `GL_LINEAR` (0x2601).

## 10. Shader warm-up [SRC], [HW-PHASE]
- **What it does:** before the first frame, `warm_up_shaders()` draws the level with each state combination (textured or not × fog on or off; textured is skipped if texture creation failed), then one HUD rectangle, then calls `glFinish()`.
- **Why:** this makes vitaGL compile its fixed-function shaders, or load them from `ux0:data/shader_cache/`, before gameplay starts.
- **Visibility:** the warm-up output is never presented; the first real frame clears it.
- **Duration:** shown in the HUD as `WARMUP n MS`. **No values recorded.** [UNVERIFIED]
- **Coverage:** state that doesn't change which shader is used (VBO versus client arrays, filtering, texture format) isn't included, and doesn't need to be.

## 11. VSync and MSAA

| Setting | Mechanism | Default | Status |
|---|---|---|---|
| VSync | `SDL_GL_SetSwapInterval(1)` at startup; R toggles 0/1; the HUD shows `SDL_GL_GetSwapInterval()` | On | [HW-PHASE] |
| MSAA | Build option `VITA_MSAA` (CMake cache, accepted values 0/2/4, anything else fails configuration); passed as a define; `SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES)` before window creation | 0 (off) | 0: [HW]; 2: [BUILD] only (temporary-folder compile); 4: [UNVERIFIED] (not compiled) |

- **To build with MSAA:** `VITA_MSAA=2 ./build.sh` or `VITA_MSAA=4 ./build.sh`.
- **The HUD's `VSYNC` value** is what SDL reports, not a measured display refresh. [SRC]

---

## 12. Current performance information
- **Owner report [HW]:** "performance is good" on phase 5 with default settings (client arrays, RGBA8 trilinear, fog on, vsync on, MSAA off).
- **Measurements:** none recorded. **No FPS, frame-time, RAM or VRAM values exist in this baseline.** [UNVERIFIED]
- **Instrumentation available in the HUD:** FPS, average and maximum frame time (0.5 s windows), `vglMemFree(VGL_MEM_RAM)` and `vglMemFree(VGL_MEM_VRAM)`, triangle and draw-call counts, estimated texture memory, warm-up time.
- **Workload [SRC]:** 488 triangles in 3 draw calls, plus the HUD, at 960×544.

---

## 13. Build instructions
Run these in **WSL Ubuntu**, not Git Bash; Git Bash rewrites paths.

```bash
cd /mnt/c/path/to/MarySkelterVita
```
```bash
./build.sh
```
```bash
VITA_MSAA=2 ./build.sh
```

- **What `build.sh` does:** sets `VITASDK=/usr/local/vitasdk` (unless it's already set), adds `$VITASDK/bin` to `PATH`, regenerates the LiveArea PNGs if they're missing, then configures and builds a Release build into `build/`.
- **Output:** `build/VitaDungeonTest.vpk`, containing `eboot.bin`, `sce_sys/param.sfo`, `icon0.png` and the LiveArea `bg.png`, `startup.png` and `template.xml`. [BUILD]
- **Build chain:** ELF → `vita-elf-create` → `.velf` → `vita-make-fself` → `eboot.bin` → `vita-mksfoex` + `vita-pack-vpk` → `.vpk`. This is handled by `vita_create_self` and `vita_create_vpk` from `vita.cmake`. [SRC]
- **Compiler flags:** `-Wall -Wextra`, C99 with GNU extensions, `-O3` (from the toolchain's Release flags), and `-Wl,-q` (added by the toolchain file). [SRC]

## 14. VPK installation
The steps below are standard homebrew practice, not verified in this project. [UNVERIFIED]

1. **Requirements:** a Vita running homebrew-enabling custom firmware, with VitaShell installed.
2. **Shader compiler:** `ur0:data/libshacccg.suprx` (or `ur0:data/external/`) must exist for vitaGL. **[HW]:** it's evidently present on the owner's device.
3. **Transfer:** copy `build/VitaDungeonTest.vpk` to `ux0:` using VitaShell's FTP or USB mode.
4. **Install:** in VitaShell, select the `.vpk` and choose Install. Accept the extended-permissions prompt if it appears.
5. **Launch:** start "Vita Dungeon Test" from LiveArea.
6. **Reinstalling:** installing a VPK with the same title ID `VDNG00001` replaces the previous version.

---

## 15. Known bugs, limitations and untested areas

### Known limitations (by design) [SRC]
- **Immediate mode is unusable** while SDL2 owns the context, because the legacy pool is 0. This is a permanent project rule.
- **Room content:** one hard-coded 12×12 room; no level loading, no doors, no multiple floors.
- **Assets:** no image decoder in the SDK; textures are procedural only; no asset pipeline.
- **Audio:** none (out of scope so far).
- **Font:** uppercase letters, digits and `+ - . : /` only.
- **Metadata:** the title ID `VDNG00001` and app name are placeholders.
- **Collision:** limited to the floor plane and the grid (§8).

### Technical gaps found during this inspection
| # | Gap | Impact |
|---|---|---|
| 1 | No device performance numbers recorded | The VBO, MSAA and 565 defaults can't be chosen on evidence yet |
| 2 | Collision test never run (no C library headers in WSL) | Collision is verified only by playing on hardware |
| 3 | MSAA 2 compiled but not tested on the device; MSAA 4 not compiled | Unknown cost and quality |
| 4 | VBO and RGB565 not individually confirmed | Both are believed to work from phase-level reports and source evidence only |
| 5 | HUD colour batch uses floats (192 KB `.bss`) | Could be 48 KB with byte colours. Memory only; not a bug. |
| 6 | Texture memory figure is an estimate | Doesn't include vitaGL padding or alignment; not measured on the device |
| 7 | SELECT texture-format switch calls `glFinish` and regenerates on the CPU | Expected visible hitch when toggled (debug only) |
| 8 | The crash dump from C2-12828-1 was never analysed | The cause was confirmed by the successful fix, not by the dump; `crash-builds/` is kept for that |
| 9 | No version control: backups are folder copies under OneDrive | No history or diffs; OneDrive also syncs `build/` and `backups/` |
| 10 | Project and folder name still contain "MarySkelter" | `TECHNICAL_AUDIT.md` §7–8 recommends a neutral name to avoid implying an official port |
| 11 | `assets/` and `docs/` are empty placeholder folders | None |

### No known functional bugs
None have been reported on hardware since the phase 0 crash fix. [HW]

---

## 16. Verification summary

| Feature | Compiled | Tested on PS Vita | Source-inspected only | Not measured / verified |
|---|---|---|---|---|
| SDL2 + vitaGL context, 960×544 | ✔ | ✔ | | |
| Immediate-mode crash cause and fix | ✔ | ✔ (crash, then fix) | ✔ (disassembly) | dump not analysed |
| Perspective camera, depth, culling | ✔ | ✔ | | |
| Textured room, mipmaps, trilinear | ✔ | ✔ | | |
| Client-array draw path | ✔ | ✔ (default) | | |
| VBO draw path | ✔ | phase-level only | ✔ | frame-time comparison |
| RGB565 textures | ✔ | phase-level only | ✔ | visual check, memory saving |
| Nearest filtering toggle | ✔ | phase-level only | | |
| Analog movement, look, deadzone | ✔ | ✔ | | |
| D-pad fallback | ✔ | phase-level only | | |
| Collision with sliding | ✔ | ✔ | | automated test |
| No-clip debug mode | ✔ | phase-level only | | |
| Fog | ✔ | ✔ (phase 5) | | cost |
| Shader warm-up | ✔ | ✔ (phase 5) | | duration |
| Vsync on (default) | ✔ | ✔ | | |
| Vsync off toggle | ✔ | phase-level only | | uncapped FPS |
| MSAA 0 (default) | ✔ | ✔ | | |
| MSAA 2 | ✔ (temporary folder) | | ✔ (SDL mapping) | ✔ |
| MSAA 4 | | | ✔ (SDL mapping) | ✔ |
| HUD and debug overlay | ✔ | ✔ | | |
| FPS / RAM / VRAM values | — | displayed | | **no values recorded** |
| Host collision test | not built | — | ✔ | ✔ |
