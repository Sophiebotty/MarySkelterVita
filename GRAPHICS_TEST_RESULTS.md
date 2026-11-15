# Graphics Compatibility and Performance Test Results

**Milestone:** graphics compatibility and performance testing (no feature work)  
**Code under test:** commit `da1b09e`, "Initial commit: Vita dungeon renderer phase 5 baseline"  
**Prepared:** 2026-11-14  
**Status:** template prepared. **No hardware results recorded yet.**  
**Update 2026-11-14:** observed (not measured) hardware results from the owner's first test session are in **§6**. The measurement tables in §3–§4 are still blank because no measurements were taken.

> Every result cell is blank until it's measured on the PS Vita. Unknown values stay blank and nothing is estimated. "Hardware verification" is `Not tested` until the owner reports a result.

---

## 1. Builds under test

| ID | Build | MSAA | Location | SHA-256 (first 16) | How the MSAA setting was checked |
|---|---|---|---|---|---|
| **B0** | Tested phase 5 VPK (reference) | 0 (off) | `MarySkelterVita/build/VitaDungeonTest.vpk` | `1676a1bbc6702b53` | Owner-tested in phase 5 |
| B0r | Rebuild, MSAA off (control) | 0 | `%USERPROFILE%\MarySkelterVita-test-builds\2026-11-14-msaa0\VitaDungeonTest.vpk` | `74fad2f8dc8120b0` | No `SDL_GL_SetAttribute` call compiled in |
| **B2** | Experimental, MSAA 2x | 2 | `%USERPROFILE%\MarySkelterVita-test-builds\2026-11-14-msaa2\VitaDungeonTest.vpk` | `82e6590abc79ec7f` | `SDL_GL_SetAttribute(14 = MULTISAMPLESAMPLES, 2)` in `main()` |
| **B4** | Experimental, MSAA 4x | 4 | `%USERPROFILE%\MarySkelterVita-test-builds\2026-11-14-msaa4\VitaDungeonTest.vpk` | `1bdc3465c8e308c4` | `SDL_GL_SetAttribute(14 = MULTISAMPLESAMPLES, 4)` in `main()` |

- **Build process:** all three experimental builds were made from `da1b09e` in separate temporary build folders, with no source changes. Each compiled with 0 warnings and 0 errors.
- **Debug files:** each experimental folder also holds the matching unstripped ELF (`VitaDungeonTest`) for crash analysis.
- **B0r vs B0:** same size, but not byte-identical, most likely from the different build folder and timestamps. Use **B0** as the MSAA-off reference.
- **Same title ID:** every build uses `VDNG00001`. Installing one **replaces** the one already on the Vita. To return to the known-good app, reinstall **B0**.
- **Identifying the installed build:** the HUD line `MSAA nX` shows which build is running. It shows the build setting, not a reading of the GPU state.

---

## 2. How to measure (use the same procedure for every row)

1. **Launch fresh.** You start at the same spot facing −Z. Don't move, so every measurement uses the same view.
2. **Wait about 5 seconds** after the HUD appears. The statistics update every 0.5 s, and the first frames include startup.
3. **Turn vsync off with R** for performance comparisons. With vsync on, the frame rate is capped at the display refresh, which can hide differences between settings.
4. **Record from the HUD:**
   - `FPS`
   - `AVG … MS` (average frame time)
   - `MAX … MS` (worst frame in the 0.5 s window)
   - `RAM …K` and `VRAM …K` (free memory)
5. **Turn vsync back on with R**, then walk around for about 30 seconds and record any stutter, glitches or crashes under "Stability".
6. **Write values exactly as the HUD shows them.** For anything that wasn't checked, leave the cell empty.

**What doesn't change during these tests:** fog on, trilinear filtering and textures on, unless a row says otherwise. Note the HUD line `TEX … / VBO / CLIENT ARRAYS` for each row.

---

## 3. Results

### Test 1: Vertex buffer objects vs client arrays (CIRCLE, build B0)

| Test | Setting | Build | Frame time AVG / MAX (ms) | Average FPS | Visual quality | Stability | Hardware verification | Notes |
|---|---|---|---|---|---|---|---|---|
| T1-a | Client arrays (default) | B0 | | | | | Not tested | |
| T1-b | VBO (press CIRCLE once) | B0 | | | | | Not tested | |

Checklist for T1-b compared with T1-a:
- [ ] Room looks identical (walls, floor, ceiling, pillars all present)
- [ ] No missing, stretched or flickering geometry
- [ ] No crash when switching, in either direction
- [ ] HUD shows `VBO`, not `VBO UNAVAILABLE`
- [ ] Frame time difference noted (vsync off)

### Test 2: RGBA8 vs RGB565 textures (SELECT, build B0)

| Test | Setting | Build | Frame time AVG / MAX (ms) | Average FPS | Visual quality | Stability | Hardware verification | Notes |
|---|---|---|---|---|---|---|---|---|
| T2-a | RGBA8 (default) | B0 | | | | | Not tested | |
| T2-b | RGB565 (press SELECT once) | B0 | | | | | Not tested | |
| T2-c | Switch back and forth (SELECT ×4) | B0 | n/a | n/a | | | Not tested | |

Checklist:
- [ ] Colours correct: no red/blue swap, tint or darkening
- [ ] Banding visible on the floor or ceiling? (565 has fewer colour levels, especially in green-to-red gradients)
- [ ] No corruption: stripes, noise, wrong tiling or broken mipmaps when far away
- [ ] HUD shows `TEX ON RGB565`, not `TEX ERROR`
- [ ] Switching causes only a short pause (expected), with no lasting hitch or crash
- [ ] Free VRAM in the HUD before and after switching (the expected saving is about 128 KB, a calculated estimate only)

### Test 3: MSAA (builds B0 / B2 / B4)

| Test | Setting | Build | Frame time AVG / MAX (ms) | Average FPS | Visual quality | Stability | Hardware verification | Notes |
|---|---|---|---|---|---|---|---|---|
| T3-a | MSAA off | B0 | | | | | Not tested | |
| T3-b | MSAA 2x | B2 | | | | | Not tested | |
| T3-c | MSAA 4x | B4 | | | | | Not tested | |

Checklist for each MSAA build:
- [ ] Installs and launches (HUD shows `MSAA 2X` / `MSAA 4X`)
- [ ] Wall and pillar edges visibly smoother than B0
- [ ] HUD text still sharp and correct
- [ ] Free VRAM in the HUD compared with B0 (MSAA needs larger render buffers)
- [ ] No crash at startup, during warm-up or while playing

### Test 4: Combined candidate (fill in after Tests 1–3)

| Test | Setting | Build | Frame time AVG / MAX (ms) | Average FPS | Visual quality | Stability | Hardware verification | Notes |
|---|---|---|---|---|---|---|---|---|
| T4-a | Best draw path + texture format + MSAA | | | | | | Not tested | |

---

## 4. Memory readings (optional, from the HUD)

| Build / setting | Free RAM (K) | Free VRAM (K) |
|---|---|---|
| B0, RGBA8, client arrays | | |
| B0, RGB565 | | |
| B0, VBO | | |
| B2 (MSAA 2x) | | |
| B4 (MSAA 4x) | | |

---

## 5. Final recommendation

**Pending hardware results.** No defaults will change until the tables above are filled in from tests on the PS Vita.

| Setting | Current default | Recommended default | Evidence |
|---|---|---|---|
| Draw path | Client arrays | | |
| Texture format | RGBA8 | | |
| MSAA | 0 | | |

---

## 6. Hardware test session: 2026-11-14 (owner-reported)

**Source:** observations reported by the project owner after testing on their modded PS Vita.  
**Nature of the results:** these are **observations, not measurements**. The HUD's FPS, frame-time, RAM and VRAM values were **not recorded** in this session.

### Result types used in this section
| Type | Meaning |
|---|---|
| **Observed** | Reported by the owner from looking at the device; no numbers |
| **Subjective** | A visual judgement the owner described as uncertain or not reliably comparable |
| **Unmeasured** | Not measured; no claim is made |

### 6.1 Summary table

| Test | Setting | Build | Installed / launched | Visual result | Visual result type | Performance | Performance type | Stability | Hardware verification |
|---|---|---|---|---|---|---|---|---|---|
| T1-a | Client arrays (VBO off) | B0 | Yes / Yes | Room looks normal | Observed | Not recorded | Unmeasured | No problems reported | **Works (observed)** |
| T1-b | VBO on (CIRCLE) | B0 | Yes / Yes | Room looks normal; no missing geometry or glitches reported | Observed | Not recorded | Unmeasured | No problems reported | **Works (observed)** |
| T2-a | RGBA8 | B0 | Yes / Yes | Original texture format | Observed | Not recorded | Unmeasured | No problems reported | **Works (observed)** |
| T2-b | RGB565 (SELECT) | B0 | Yes / Yes | Switched successfully; room looked normal | Observed | Not recorded | Unmeasured | No problems reported | **Works (observed)** |
| T2-c | Switch RGB565 back to RGBA8 | B0 | Yes / Yes | Switched back afterwards | Observed | n/a | n/a | No problems reported | **Works (observed)**, one switch each way |
| T3-a | MSAA off (reference) | B0 | Yes / Yes | Known-good reference | Observed | Not recorded | Unmeasured | Known good | **Works**: phase 5 reference |
| T3-a′ | MSAA off rebuild | B0r | **Not installed** | — | — | — | — | — | **Not tested** |
| T3-b | MSAA 2x | B2 | Yes / Yes (see note) | Improvement over MSAA off **inconclusive**; the owner couldn't reliably recall the original look | Subjective | "Smooth" | Observed, unmeasured | No problems reported | **Installs and runs (observed)**; visual benefit unverified |
| T3-c | MSAA 4x | B4 | Yes / Yes | Owner thought edges looked **slightly smoother, but was uncertain** | Subjective | "Smooth" | Observed, unmeasured | No problems reported | **Installs and runs (observed)**; visual benefit unverified |

### 6.2 Notes
- **VBO:** both draw paths showed the room normally. That confirms the VBO path renders correctly on hardware, which was previously only checked in source and at phase level. **Whether it is faster or slower is unknown;** no frame times were recorded.
- **RGB565:** colours and geometry looked normal. **Banding wasn't specifically assessed** and no texture corruption was reported. The memory saving (about 128 KB, calculated in §5 of `TECHNICAL_BASELINE.md`) is **not confirmed by a VRAM reading**. Fewer bytes per texture doesn't by itself mean better performance or quality, so RGB565 isn't assumed to be better.
- **MSAA 2x install history:** an earlier install attempt of B2 failed in VitaShell with error `0xFFFFFFFF`. A read-only inspection then found the B2 VPK file valid: ZIP integrity OK, all required files present, `param.sfo` identical to the reference, `TITLE_ID` `VDNG00001`. In this session B2 **installed successfully**. **The cause of the earlier failure was not determined.**
- **MSAA 2x vs 4x:** both installed, launched and felt smooth. **Neither is shown to be visually or technically better than the other, or than MSAA off.** The visual comparisons weren't side by side and the owner reported them as uncertain.
- **"Smooth" performance:** this is the owner's impression with the default settings (vsync on). With vsync on, the frame rate is capped, so the impression can't distinguish the cost of MSAA, VBO or RGB565.

### 6.3 What remains untested or unmeasured
| Item | Status |
|---|---|
| FPS and AVG/MAX frame time for each setting (vsync off), per §2 | Unmeasured |
| Free RAM and VRAM for each setting (§4 table) | Unmeasured |
| VBO vs client-array performance difference | Unmeasured |
| RGB565: banding check, VRAM saving, performance | Unmeasured / not assessed |
| MSAA 2x and 4x: frame-time cost and VRAM cost | Unmeasured |
| MSAA visual quality: side-by-side comparison against MSAA off | Not done (subjective only) |
| MSAA off rebuild (B0r) | Not installed |
| Repeated format switching (SELECT several times) | Not tested |
| Cause of the earlier B2 `0xFFFFFFFF` install failure | Not determined |
| Combined candidate configuration (T4-a) | Not tested |

### 6.4 Effect on the final recommendation (§5)
**None yet.** The observed results show that every tested option **works without visible problems**, but they don't provide measurements to choose defaults on. §5 stays pending. The current defaults (client arrays, RGBA8, MSAA off) are unchanged.
