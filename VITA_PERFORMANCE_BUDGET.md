# PS Vita Performance Budget (Provisional)

**Phase:** 6, performance budget  
**Date:** 2026-11-14  
**Repository state at writing:** `1aee728` (unchanged by this document)  
**Status:** provisional. Every target here is an engineering proposal derived from **one** benchmark run per MSAA setting. It must be re-checked against the repeat runs listed in §8.

### Inputs
| Source | Used for |
|---|---|
| `benchmark-data/results_msaa0.csv`, `results_msaa2.csv`, `results_msaa4.csv` (SHA-256 `e3e031fc…`, `5fc557be…`, `e2f1a310…`) | All measured numbers |
| `BENCHMARK_RESULTS.md` | Derived differences and ratios, and limitations |
| `bench/src/bench_main.c`, `bench_tests.c` | How the values were measured (§0) |
| `GRAPHICS_TEST_RESULTS.md` | Hardware observations: VBO, RGB565 and MSAA 2x/4x render or run correctly |
| `TECHNICAL_BASELINE.md` | Current engine configuration and defaults |
| `MARY_SKELTER_VITA_FEASIBILITY.md` | API limits (no compute or tessellation in vitaGL) and project context |

### Labels
| Label | Meaning |
|---|---|
| **MEASURED** | Copied from the CSVs, or simple arithmetic on CSV values with the operands shown |
| **TARGET (proposed)** | An engineering limit chosen for future work. Not a measurement; it can and should change when better data exists |
| **INFERRED** | Reasoning from measurements that the data doesn't directly prove |
| **UNKNOWN** | Not measured |

---

## 0. What the measurements are (MEASURED / method)
- **Hardware:** the project owner's modded PS Vita, with the SDL2 + vitaGL stack and vsync **off**.
- **Frame time:** the interval between consecutive frame starts, measured on the CPU with `SDL_GetPerformanceCounter`. There's **no GPU timing**, so CPU submission and GPU execution aren't separated.
- **Per test:** 1 s warm-up, then 5 s of samples. **One run per MSAA build.**
- **Overlay included:** every frame includes clear, depth test and VitaBench's own overlay. The EMPTY test (1.778 / 1.931 / 2.307 ms for off / 2x / 4x) is that fixed baseline.
- **Synthetic content:**
  - geometry: about 1 px² triangles in a 256×256 px area
  - fill: full-screen quads at about 1 texel per pixel, all at the same depth
  - textures: 128×128 procedural

---

## 1. Frame-rate and frame-time targets

| Item | Value | Label |
|---|---|---|
| Frame time at 60 fps | 16.667 ms | Arithmetic |
| Frame time at 30 fps | 33.333 ms | Arithmetic |
| Fixed per-frame interval (clear + swap + overlay) | 1.778 / 1.931 / 2.307 ms (off / 2x / 4x), measured as the **CPU-side interval between frame starts** | MEASURED |
| **Primary target** | **30 fps, stable**: P95 frame time ≤ 33.3 ms with vsync on | TARGET (proposed) |
| **Stretch target** | **60 fps** for exploration scenes where the budget allows: P95 ≤ 16.7 ms | TARGET (proposed) |
| **Rendering allocation** | ≤ **50 %** of the frame for rendering, as measured by the frame-start interval method (≤ 16.7 ms at 30 fps, ≤ 8.3 ms at 60 fps); the rest goes to game logic, audio, streaming and safety margin. A **planning allocation**, **not** a measured GPU-time limit | TARGET (proposed) |
| GPU rendering time, separate from CPU submission | — | UNKNOWN (no GPU timer was used; see the note below and §8, V12) |
| Game logic, audio and streaming cost | — | UNKNOWN (nothing exists to measure) |
| Whether vitaGL honours a swap interval of 2 (fixed 30 fps) | — | UNKNOWN (only 0 and 1 have been used) |

**What the timing numbers mean.** Every millisecond value in this document comes from VitaBench's **CPU-side frame-start interval**: the time from one frame's start to the next, measured with `SDL_GetPerformanceCounter`.
- That interval covers the CPU work of issuing GL calls and swapping buffers. The GPU work shows up in it only when the CPU has to wait for the GPU, for example at buffer swap or when vitaGL's queues fill.
- It **does not** measure GPU rendering time independently, and it can't show how a frame divides between CPU and GPU.
- So the 16.7 ms (30 fps) and 8.3 ms (60 fps) rendering allocations are **engineering planning targets expressed in that same interval unit**. They are **not** measured GPU capacities, and meeting them doesn't guarantee a scene is GPU-safe.
- Confirm real scenes on hardware with vsync on (§8, V11–V12).

**Why 30 fps first (INFERRED):** a dungeon RPG with turn-based or menu-driven combat tolerates 30 fps well. The one heavy synthetic scene measured (200K textured triangles) already took 9.577–11.860 ms. Blending can use most of a 60 fps frame on its own (§5). Planning for 60 fps everywhere would constrain content far more than these numbers justify.

---

## 2. Component cost table: the core of the budget (MEASURED)

These are **marginal costs** for use in planning. Each is a difference between two measured tests in the same build, in **CPU-side frame-start interval** milliseconds (§1 note), not GPU time; the operands are in `BENCHMARK_RESULTS.md`. The "planning value" column is the **worst of the three builds**, used to stay conservative.

| Cost component | MSAA off | MSAA 2x | MSAA 4x | Planning value (worst) | Source tests |
|---|---|---|---|---|---|
| Fixed frame baseline | 1.778 | 1.931 | 2.307 | **2.31 ms** | 1 |
| 10K flat triangles above baseline | 0.306 | 0.379 | 0.306 | **0.38 ms** | 2 − 1 |
| 50K flat triangles above baseline (VBO) | 2.080 | 1.618 | 1.700 | **2.08 ms** | 3 − 1 |
| 50K textured triangles above baseline (VBO) | 2.252 | 1.998 | 2.055 | **2.26 ms** | 6 − 1 |
| 100K flat triangles above baseline (VBO) | 4.737 | 3.571 | 3.281 | **4.74 ms** | 4 − 1 |
| 200K textured triangles above baseline (VBO) | 10.082 | 7.646 | 7.677 | **10.09 ms** | 7 − 1 |
| Extra opaque full-screen layer | 0.0479 | 0.1179 | 0.2077 | **0.21 ms** | (15 − 12) / 7 |
| Extra **blended** full-screen layer | 1.5170 | 1.7695 | 1.6962 | **1.77 ms** | (17 − 16) / 4 |
| Fog on a 4-layer fill scene | +1.827 | +1.526 | +0.934 | **1.83 ms** | 23 − 14 |
| Fog on a 200K textured scene | +1.620 | +0.768 | +0.222 | **1.62 ms** | 24 − 7 |
| Going from 10 to 500 draw calls (50K triangles) | −0.057 | +0.181 | +0.101 | **0.18 ms** | 10 − 8 |
| Going from 500 to 1000 draw calls | +1.125 | +1.326 | +1.318 | **1.33 ms** | 11 − 10 |
| Client arrays instead of VBO (50K) | +3.517 | +3.846 | +3.520 | **3.85 ms** | 18 − 19 |

The 200K row is arithmetic on the CSV: 11.860 − 1.778, 9.577 − 1.931 and 9.984 − 2.307.

**Caution (INFERRED):** adding these components together assumes the costs are independent and add up linearly. The benchmark never measured a combined scene, so a budget built by addition is a **planning estimate**, not a prediction (see §8, item V6).

---

## 3. Draw-call budget

| Item | Value | Label |
|---|---|---|
| Change from 10 to 500 draws (50K triangles) | −0.057 to +0.181 ms | MEASURED |
| Change from 500 to 1000 draws | +1.125 to +1.326 ms (about 2.3–2.7 µs per extra draw) | MEASURED |
| Cost of **state changes** between draws (texture binds, matrices, shader switches) | — | UNKNOWN (the benchmark's draws only change index ranges) |
| **Soft limit** | **≤ 300 draw calls per frame** | TARGET (proposed) |
| **Hard limit** | **≤ 500 draw calls per frame** | TARGET (proposed) |

**Reasoning (INFERRED):** cost was flat up to 500 cheap draws, but real draws include state changes that weren't measured. A soft limit of 300 leaves room for that unknown cost; 500 is the highest count with measured evidence of low cost. Batch static level geometry by material: the current room needs 3 draws (`TECHNICAL_BASELINE.md`).

---

## 4. Geometry budget

| Item | Value | Label |
|---|---|---|
| 50K textured triangles | 4.030 / 3.929 / 4.362 ms total (2.26 ms worst marginal) | MEASURED |
| 100K flat triangles | 6.515 / 5.502 / 5.588 ms total | MEASURED |
| 200K textured triangles | 11.860 / 9.577 / 9.984 ms total | MEASURED |
| Marginal frame-interval cost per **flat** triangle at 200K, i.e. (test 5 avg_ms − test 1 avg_ms) ÷ 200000 | Off: (11.242 − 1.778) ms ÷ 200000 = 9.464 ms ÷ 200000 = **47.32 ns**. 2x: (8.537 − 1.931) ÷ 200000 = 6.606 ms ÷ 200000 = **33.03 ns**. 4x: (8.929 − 2.307) ÷ 200000 = 6.622 ms ÷ 200000 = **33.11 ns** | MEASURED (arithmetic on CSV `avg_ms`; test 5 = TRIS 200K FLAT, test 1 = EMPTY CLEAR SWAP) |
| Cost of realistically sized triangles, skinned or animated meshes, or per-vertex lighting | — | UNKNOWN |
| **Visible triangles per frame at 30 fps** | **≤ 100K** (about 4.74 ms marginal, worst flat case) | TARGET (proposed) |
| **Visible triangles per frame at 60 fps** | **≤ 50K** (about 2.26 ms marginal, worst textured case) | TARGET (proposed) |
| Static geometry | **VBO / IBO only** (§6.1) | TARGET (proposed) |

Notes on the per-triangle row:
- It is a **CPU-side frame-interval** cost averaged over 200K tiny (about 1 px²) triangles in a 256×256 px area, drawn from a VBO in 7 draws. It isn't a GPU throughput figure and doesn't transfer to other triangle sizes.
- The off-vs-MSAA difference (47.32 vs about 33 ns) is part of the unexplained MSAA anomaly (§8, V1). Don't pick one value until it's repeated.
- The textured equivalent (test 7 − test 1), from the same CSVs, is 50.41 / 38.23 / 38.38 ns.

**Reasoning (INFERRED):** the benchmark's triangles are tiny and cover a small area, so they mostly measure vertex and submission cost. Real meshes have larger triangles that cost more pixels, and animated characters cost CPU. The targets use **about half or less** of what was measured as affordable, leaving that margin.

---

## 5. Fill, transparency and fog budget

| Item | Value | Label |
|---|---|---|
| Extra opaque full-screen layer | 0.0479 / 0.1179 / 0.2077 ms | MEASURED |
| Extra blended full-screen layer | 1.5170 / 1.7695 / 1.6962 ms | MEASURED |
| 8 blended full-screen layers | 13.289 / 15.239 / 15.386 ms total | MEASURED |
| Opaque overdraw with **different** depths, alpha test, or complex shaders | — | UNKNOWN (all layers were at the same depth) |
| **Blended pixel budget** | **≤ 2 full-screen equivalents per frame** (about 3.5 ms worst case). Example: one full-screen fade plus a half-screen UI panel and particles | TARGET (proposed) |
| **Opaque overdraw** | **≤ 3× average**; sort opaque geometry roughly front to back | TARGET (proposed) |
| **Fog** | Allowed; **budget 2 ms** (worst measured 1.827 ms) | TARGET (proposed) |

**Opaque vs blended tradeoff (MEASURED → INFERRED):**
- Blending was **about 8–32× more expensive** per full-screen layer than opaque drawing. The measured ratios are 1.5170 ÷ 0.0479, 1.7695 ÷ 0.1179 and 1.6962 ÷ 0.2077.
- **Design rule:** transparency is the scarcest rendering resource on this stack.
  - Prefer opaque or alpha-tested geometry wherever it looks acceptable.
  - Keep full-screen fades, vignettes and stacked translucent UI to a minimum.
  - Draw translucent particles small and few.
- Opaque layers measured cheap, but only in a same-depth test that may favour the GPU (INFERRED: tile-based hidden-surface removal). Don't treat opaque overdraw as free.

---

## 6. Feature tradeoffs

### 6.1 VBOs vs client arrays
| | VBO | Client arrays |
|---|---|---|
| 50K flat triangles, 2 draws | **3.862 / 3.580 / 4.040 ms** | 7.379 / 7.426 / 7.560 ms |
| Ratio (client ÷ VBO) | — | 1.911 / 2.074 / 1.871 |
| Frame spread (max − min) | 0.926 / 0.765 / 0.930 ms | 1.806 / 1.666 / 1.770 ms |
| Renders correctly on hardware | Yes (observed in `GRAPHICS_TEST_RESULTS.md` and VitaBench) | Yes |

The spread row is calculated from the CSV min/max values (MEASURED).

**Tradeoff:** client arrays need no buffer management and suit data that changes every frame (the overlay, particles). VBOs avoid the per-frame copy that vitaGL makes for client arrays (seen in the disassembly; `TECHNICAL_BASELINE.md` §6) and were consistently about twice as fast.

**Decision (TARGET):** VBO/IBO for all static geometry. Client arrays only for small, per-frame dynamic data such as the overlay.

### 6.2 MSAA off / 2x / 4x
| | Off | 2x | 4x |
|---|---|---|---|
| VRAM free at startup (KB) | 84930 | 82380 (−2550) | 77280 (−7650) |
| Fixed frame baseline (ms) | 1.778 | 1.931 | 2.307 |
| 8 opaque full-screen layers (ms) | 2.307 | 2.927 | 4.161 |
| Fill-test cost vs off (tests 12–15, 20, 21) | — | +0.130 to +0.654 | +0.735 to +1.951 |
| Geometry-heavy tests vs off | — | **faster** (inconsistent) | **faster** (inconsistent) |
| Visual benefit on device | — | inconclusive (owner) | "slightly smoother", uncertain (owner) |

**Tradeoff:** MSAA has a measured **VRAM cost** and a measured **fill cost** that grows with the number of samples. Its visual benefit is unconfirmed. Its effect on geometry-heavy scenes is **not trustworthy yet** (§8, V1).

**Decision (TARGET):** **MSAA off** by default. Re-evaluate **2x** after the repeat runs; 4x isn't a candidate on current evidence (higher fill cost, 7.5 MB of VRAM, no confirmed visual gain over 2x).

### 6.3 RGBA8 vs RGB565
| | RGBA8 | RGB565 |
|---|---|---|
| Fill 4 layers (ms) | 2.066 / 2.421 / 3.335 | 2.005 / 2.288 / 3.234 |
| Fill 8 layers (ms) | 2.307 / 2.927 / 4.161 | 2.128 / 2.782 / 4.079 |
| 200K textured (ms) | 11.860 / 9.577 / 9.984 | 11.840 / 9.567 / 9.962 |
| VRAM per 128×128 texture with mips | about 87 KB (calculated) | about 43 KB (calculated) |
| VRAM saving | — | **UNKNOWN**: not recorded by VitaBench |
| Alpha channel | Yes | **No** |
| Banding and image quality on device | Reference | **Not assessed** (looked "normal" to the owner) |

**Tradeoff:** RGB565 was slightly faster in all 9 pairs, by 0.010–0.179 ms, and should halve memory (calculated), but it has no alpha and fewer colour levels.

**Decision (TARGET):**
- **Default stays RGBA8** while quality and the memory saving are unverified.
- Use **RGB565 per texture** for large opaque textures (walls, floors, skies) after a visual check.
- Never use RGB565 for textures that need alpha.
- Compressed formats (DXT, PVRTC, ETC1 are defined in vitaGL) are UNKNOWN and need testing (§8, V9).

---

## 7. Memory budget

| Item | Value | Label |
|---|---|---|
| VRAM free at VitaBench startup | 84930 KB (off), 82380 KB (2x), 77280 KB (4x), measured **after** VitaBench's own resources (both texture sets, grid VBO/IBO) | MEASURED |
| **Additional** textures allocated without error, on top of resources VitaBench already held (both 128×128 texture sets with mips, grid VBO/IBO, overlay) | 48 × 1 MB (512×512 RGBA8, no mips) in every build, reducing free VRAM by exactly 49153 KB; the probe **stopped at its own 48-texture safety cap**, not at an error or a memory limit | MEASURED |
| VRAM still free after +48 MB | 35777 / 33227 / 28127 KB | MEASURED |
| VRAM fully recovered after deleting | Yes (back to 84930 / 82380 / 77280 KB) | MEASURED |
| vitaGL RAM-pool free (`VGL_MEM_RAM`) | 64973 KB in every build and every test; textures didn't use it | MEASURED |
| Total system RAM free, heap use, application memory budget | — | **UNKNOWN** |
| Maximum or total texture memory available | — | **UNKNOWN**. The benchmark established **neither a total texture-memory limit nor a 48 MB ceiling**; it only showed that 48 MB *more* fit, with 28127–35777 KB still free afterwards |
| **Total resident texture budget (planning value)** | **≤ 48 MB** of textures resident in VRAM, in total. Chosen so that a complete scene's textures stay **within the amount of *additional* allocation already shown to work** (48 MB beyond a small baseline), with the remaining free VRAM as headroom. It's a conservative planning number, **not** a measured capacity; raise it once V10 measures the real limit | TARGET (proposed) |
| **VRAM safety margin** | **≥ 16 MB free** at all times. A provisional engineering margin, copied from the probe's own stop rule (`TEXMEM_MARGIN_KB`); no measurement showed 16 MB to be required. For reference, the lowest free VRAM observed after the +48 MB allocation was 28127 KB (4x) | TARGET (proposed) |
| **MSAA VRAM reserve** | 2.5 MB if 2x is ever enabled; 7.5 MB for 4x | MEASURED (2550 / 7650 KB) |
| **CPU-side RAM budget** | **To be set after measurement**. Until then, treat vitaGL's RAM pool (about 63 MB free) as a soft ceiling for GPU-related allocations and keep CPU heap use modest | TARGET (proposed) / UNKNOWN basis |
| Geometry memory | The current room's VBO is 23,424 B + 2,928 B IBO (from the source code); negligible | Calculated from source |

---

## 8. Results that need repeating or further validation

| # | Item | Why | How to validate |
|---|---|---|---|
| **V1** | **MSAA faster than off in 16 comparisons** (for example 200K flat: 8.537 / 8.929 vs 11.242 ms) | Physically implausible as a real effect; single runs at different times; the MSAA-off run had about twice the frame spread (largest 3.325 ms vs 1.678 / 1.770) | Run each build **at least 3 times**, alternating order (off → 2x → 4x → off …), same temperature and charger state; compare medians |
| V2 | Fog cost varies between builds (+0.222 to +1.827 ms) | Same anomaly pattern as V1 | Same repeat protocol |
| V3 | VBO advantage (about 2×) | Consistent but single-run | Confirm in the repeat runs; then switch the main app to VBOs (separate approval) |
| V4 | RGB565 VRAM saving | Not written to the CSV | Log the texture-set VRAM cost to the CSV |
| V5 | RGB565 image quality | Not assessed | Side-by-side screenshots of the same view on the Vita |
| V6 | Whether costs add up | No combined scene was measured | Add a "budget scene" test combining the §9 example |
| V7 | Draw calls with state changes | Only index-range draws were measured | Add tests that change texture or matrix per draw |
| V8 | Realistic triangle sizes, skinned meshes | Tiny triangles only | Add a test with larger triangles across the screen and depth variation |
| V9 | Compressed texture formats | Not tested | Add DXT/PVRTC/ETC1 upload and sampling tests |
| V10 | Real VRAM limit and total system RAM | Probe capped; RAM not measured | Raise the cap gradually and log system free memory (API to be identified) |
| V11 | Frame pacing with vsync on | All tests had vsync off | Repeat key tests with swap interval 1 (and 2 if supported) |
| V12 | CPU vs GPU time | Only the CPU-side interval was measured | Investigate GPU timing options in vitaGL/GXM (UNKNOWN availability) |

Items V4 and V6–V12 need **VitaBench code changes**, which this document doesn't make.

---

## 9. Worked example: a 30 fps dungeon frame (planning estimate)
This is built **only** from §2 planning values (worst build per component). It's an INFERRED planning estimate that assumes costs add up, which is unverified (V6). All values are **CPU-side frame-start interval** costs (§1 note), **not** GPU rendering time.

| Component | Planning cost |
|---|---|
| Fixed baseline | 2.31 ms |
| 100K visible triangles (flat marginal; textured adds about 0.62–1.06 ms per 200K, measured) | 4.74 ms + about 0.53 ms texture allowance |
| Fog | 1.83 ms |
| 2 full-screen equivalents of blended pixels | 3.54 ms |
| 3× opaque overdraw (2 extra layers) | 0.42 ms |
| Up to 300 draw calls (state changes unknown) | 0.18 ms measured + **unknown** |
| **Estimated rendering total** (frame-interval units) | **about 13.6 ms + unknown state-change cost** |
| Rendering allocation at 30 fps (§1): a planning target, not a measured GPU limit | ≤ 16.7 ms |

The texture allowance is 0.53 ms ≈ half of the 1.055 ms measured for 200K textured minus flat (4x), scaled to 100K.

**Reading:** on current evidence this content level fits the proposed 30 fps **planning allocation** with about 3 ms to spare, **before** unknown state-change, CPU-side game, animation and audio costs. Because the inputs are frame-interval measurements, this says nothing independent about GPU load. A scene that fits on paper still has to be confirmed on hardware with vsync on (V11) and, if possible, with GPU timing (V12). The same content would **not** fit a 60 fps frame. For 60 fps, roughly halve the geometry, use one blended screen equivalent, and consider dropping fog.

---

## 10. Recommended conservative default configuration (TARGET, proposed)

| Setting | Default | Reasoning |
|---|---|---|
| Frame-rate target | **30 fps stable**, 60 fps where affordable | §1, §9 |
| Vsync | **On** | Avoids tearing; current app default; pacing to be verified (V11) |
| Static geometry | **VBO / IBO** | About 2× faster in every build (§6.1); renders correctly on hardware |
| Dynamic or overlay geometry | Client arrays | Small per-frame data; proven path |
| MSAA | **Off** | Measured VRAM and fill cost; visual gain unconfirmed; geometry results unreliable (V1) |
| Texture format | **RGBA8 default**; **RGB565 opt-in** per opaque texture after visual check | Quality and saving unverified (§6.3) |
| Texture filtering | Trilinear with mipmaps (current default) | Works on hardware; filtering cost wasn't benchmarked (UNKNOWN) |
| Fog | **On**, within the 2 ms budget | Hides the far plane; measured cost acceptable at 30 fps |
| Draw calls | ≤ 300 soft / ≤ 500 hard | §3 |
| Visible triangles | ≤ 100K at 30 fps, ≤ 50K at 60 fps | §4 |
| Blended pixels | ≤ 2 full-screen equivalents | §5 |
| Texture VRAM | ≤ 48 MB total resident (planning value), ≥ 16 MB free (provisional margin) | §7. Neither is a measured limit |
| Immediate mode | **Never** (SDL2 initialises vitaGL with legacy pool 0) | `TECHNICAL_BASELINE.md` §2 |
| Compute / tessellation | **Not available**; don't design features that need them | vitaGL symbols absent (`MARY_SKELTER_VITA_FEASIBILITY.md` §3) |

**Relation to the working app:** the app's current defaults (client arrays, RGBA8, MSAA off, fog on, vsync on) are **unchanged**. Moving the static level geometry to VBOs is the only recommended change, and it needs a separate approval and hardware re-test.

---

## 11. Engine decision rules (for future design work)
These are proposed working rules derived from the measurements, not measurements themselves:

1. **Transparency costs about 1.5–1.8 ms per full-screen equivalent.** Ask "how many screens of blended pixels does this add?" before adding any effect.
2. **Static meshes go in VBOs.** Client arrays cost about 3.5–3.8 ms more per 50K triangles.
3. **Batch by material; stay under 300 draws.** Don't spend the 500–1000 region, which costs about 1.1–1.3 ms more.
4. **Budget geometry per scene, not per object:** 100K visible at 30 fps, 50K at 60 fps, including characters.
5. **Keep about 16 MB of VRAM free** (provisional margin); **plan total resident textures inside 48 MB** as a conservative planning value. The benchmark showed 48 MB of *additional* textures fit; the real limit is unmeasured.
6. **Treat MSAA as an optional late-stage setting,** decided only after repeat measurements.
7. **Don't rely on GPU features vitaGL lacks** (compute, tessellation) or on immediate mode.
8. **Every new rendering feature gets a VitaBench test before it ships,** and the budget is updated with the measured cost.

---

## 12. What these measurements do NOT prove about Mary Skelter Finale

VitaBench is **synthetic** and measured **only our own SDL2 + vitaGL stack**. It does **not** show:

- **That Finale can run on the Vita.** No Finale code, engine, data or asset was used or measured.
- **What Finale's frames contain.** Its triangle counts, draw calls, texture sizes, overdraw, transparency use, post-processing (YEBIS) and animation load are **unknown**, so nothing here can be compared with them.
- **How Finale's engine would perform.** The game is built on its own engine (Gx framework, with OROCHI strongly indicated but not officially confirmed) and Direct3D 11 / Mizuchi-class rendering. Our numbers describe vitaGL's fixed-function path, not that engine or a hypothetical OROCHI Vita back-end.
- **CPU-side game cost.** Game logic, scripting, AI, audio (CRI ADX2), video (Sofdec2), file I/O and streaming were not measured at all.
- **Real-world memory fit.** Finale's memory needs are unknown, and the Vita's total available system RAM wasn't measured.
- **That a port is feasible or permitted.** Feasibility depends on rights-holder access to code, engine, middleware and assets (`MARY_SKELTER_VITA_FEASIBILITY.md` §9–§13). Performance numbers don't change that.

What the measurements **do** provide: a measured cost model of this Vita for basic rendering operations. It can size any future content (original or licensed) and gives concrete numbers for discussing Vita constraints with a rights holder.

---

## 13. Summary
| Category | Status |
|---|---|
| Measured component costs (§2) | Available from one run per build; consistent for VBO, blending, draw calls and fill; **unreliable** for MSAA-vs-off geometry comparisons and the size of fog cost |
| Proposed targets (§1, §3–§5, §7) | Defined as provisional engineering limits with margins |
| Conservative default configuration (§10) | 30 fps, vsync on, VBO for static geometry, MSAA off, RGBA8 default (RGB565 opt-in), fog on |
| Required validation (§8) | Repeat runs (V1–V3) first; then VitaBench additions (V4–V12) |
| Unknowns | State-change cost, realistic triangle sizes, combined scenes, compressed textures, real VRAM limit, system RAM, CPU vs GPU split, vsync-on pacing |
| Relevance to Mary Skelter Finale | None directly (§12) |
