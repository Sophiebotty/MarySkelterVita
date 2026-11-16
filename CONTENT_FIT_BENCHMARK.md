# Vita ContentFit: Stage 4 content-fit benchmark

**Status:** built, tested on the PC, and run on the PS Vita: run 001 on 2026-11-15, 3 iterations, all 289 result rows `OK`. Summary under "Hardware results (run 001)" below; full report in `CONTENTFIT_RESULTS.md`. The results describe this Vita and this project's stack, not Mary Skelter Finale.
**Title ID:** `VDNG00005` ("Vita ContentFit", version 01.00), separate from VDNG00001–00004.
**Location:** `contentfit/` (sources, CMake, build script), `tools/make_contentfit_fixtures.py`,
`tools/check_contentfit_fixtures.py`, `tools/analyze_contentfit_results.py`, `tests/host/contentfit/` and
`tests/host/run_contentfit_host_tests.sh`. Build output: `build-contentfit/` (git-ignored).

## What it is, and what it is not
- **What it measures:** how much synthetic dungeon, battle, dialogue and menu content this Vita can draw at a stable 30 fps, how long synthetic areas take to load, and how memory behaves. It uses this project's SDL2 + vitaGL stack.
- **What it uses:** synthetic content only. It reads no Mary Skelter file. Sizes come from the generic Stage 4 tiers, not from Finale.
- **What it cannot show:** the results describe this Vita and this stack. They are **not** measurements of Mary Skelter Finale.
- **Reused code, compiled unmodified from its own location:**
  - Phase 8 `rp_io`, `rp_mem`, `rp_inflate`, `rp_time`
  - the GDAT Proto reader: `gp_common`, `gp_gdat`, `gp_wrap`
  - `src/graphics/hud2d.c` and `src/input/input.c`

## Safeguards
| Rule | How it is enforced |
|---|---|
| Synthetic data only | Fixtures are generated on the PC, checked by an independent reader, then packaged. Reads are allowed only from `app0:fixtures/` and `ux0:data/ContentFit/work/`. Paths containing `..` are refused. |
| Output in its own folder | Writes are allowed only below `ux0:data/ContentFit/`. SDL's log is redirected into the run's log. vitaGL's shader cache is redirected to `ux0:data/ContentFit/shader_cache` (next section). |
| Never overwrite | A run number is used only if none of its four files exist. Each file is created only after a check that it does not exist. The work copy `work/io16.bin` is created once and reused only if its CRC matches; it is never replaced. |
| Never delete | The app deletes no file. |
| Memory | Every step is checked against the measured pools **before** it allocates anything. During allocation it is checked again after every chunk (see below). |

### vitaGL's shader cache: redirected into ContentFit's folder
The installed vitaGL (`/usr/local/vitasdk/arm-vita-eabi/lib/libvitaGL.a`) uses a shader cache at fixed paths:
- `vglInitWithCustomSizes` (in `vgl.o`) calls `sceIoMkdir` four times, creating `ux0:data/shader_cache/…`.
- `reload_ffp_shaders` (in `ffp.o`) calls `sceIoOpen`, then reads and writes `.gxp` files there.
- `vglSetShaderCachePath` is an empty function in this build, so the cache cannot be moved through the API.

Every other app in this project that uses SDL2 + vitaGL (GDAT Proto, ResProto, VitaBench and the main app) has this behaviour unchanged.

**ContentFit redirects it at link time** (owner's decision, 2026-11-15, replacing an earlier accidental "accept"):
- `contentfit/CMakeLists.txt` links with `--wrap=sceIoOpen --wrap=sceIoMkdir`, so every call to these two functions in the program goes through `contentfit/src/cf_iowrap.c`.
- A path at or below `ux0:data/shader_cache` is rewritten to the same path below `ux0:data/ContentFit/shader_cache`. Every other path is passed on unchanged.
- A rewritten path that would not fit the wrapper's buffer is refused with an error, so nothing is ever created at the default location.
- vitaGL and every other module are unmodified. A scan of every non-stub library ContentFit links found these two calls to be vitaGL's only file-system calls with paths. The other libraries open only paths given to them by the program.
- `main()` creates `ux0:data/ContentFit` before SDL starts vitaGL, so the redirected folders can be created.
- The run log reports how many folder creations and file opens were redirected, and how many were refused.
- ContentFit never deletes the cache. Shader compilation on first use is absorbed by the warm-up before every measurement.

## Running it
1. **Build (WSL):**
   ```bash
   bash contentfit/build_contentfit.sh
   ```
   This regenerates the fixtures if they are missing and always runs the independent fixture checker first.
2. **Install:** install `build-contentfit/VitaContentFit.vpk` (about 54 MB) with VitaShell.
3. **Run:**
   - Connect the charger.
   - On the start screen, press **CROSS** for a full run (3 iterations; run 001 took about 25 minutes) or **SQUARE** for a quick run (1 iteration, estimated at about 10 minutes). **START** exits.
   - During a run, **START** stops it after the current step; everything written so far is kept.
   - On the results screen, **CIRCLE** starts a new run (with a new run number) and **START** exits.
4. **Send back** the four files of the run from `ux0:data/ContentFit/`:
   - `runNNN.csv`
   - `runNNN.log`
   - `runNNN_frames.csv`
   - `runNNN_progress.txt`

   If the app stops unexpectedly, the last line of `runNNN_progress.txt` names the step that was running.

The quick-run time is an estimate from the schedule and GDAT Proto's measured read speed (about 2.4 MB/s from `app0:`); it has not been measured. The full run took 1,513.4 s on the device (run 001).

## Tests (one iteration, in this order)
| Group | IDs | What it measures | Result |
|---|---|---|---|
| C0 | C0-01 | Pool sizes and free space (vitaGL VRAM, RAM, PHYCONT, CDLG; C library heap block; kernel free memory), clocks, battery, ux0 free space. Passive only | Logged |
| C1 storage | C1-01..05 | 16 MiB read with stdio at 16 KiB / 64 KiB / 256 KiB / 1 MiB / 4 MiB chunks, from `app0:` | MiB/s; CRC checked |
| | C1-06..08 | The same with direct `sceIoRead` at 64 KiB / 1 MiB / 4 MiB | MiB/s; CRC checked |
| | C1-09 | One-time copy to `ux0:data/ContentFit/work/io16.bin` (created once, verified, never replaced) | Write rate when copied |
| | C1-10 / 11 | 1 MiB reads of that copy (raw / stdio) | MiB/s |
| | C1-12 / 13 | 512 reads of 16 KiB in order / in a fixed shuffled order | ms per read; random-access penalty |
| | C1-14 / 15 | The same file read twice back to back | Shows any cache effect |
| C2 loads | C2-08/16/32/64 SERIAL | GDAT-style packs of 8/16/32/64 MiB of payload, read with the unmodified GDAT Proto reader, then inflated and verified. Textures are uploaded to VRAM; data stays in the heap until the pack is done | Total ms with phase breakdown; targets: **8 MiB ≤ 2 s**, **32 MiB ≤ 10 s** |
| | C2-…PIPE | The same, with a worker thread reading while the main thread inflates and uploads | Same |
| | C2-PORT | One 512×1024 portrait entry: read + inflate + verify + upload | Target **≤ 0.5 s** |
| C3 pacing | C3-P | Empty scene at swap interval 2. If the measured median isn't 33.3 ± 1 ms, the app uses swap interval 1 plus a wait for every second vertical blank | Method used by every LOCK30 run in that iteration |
| C3 scenes | C3-D/B/V/U-S..XL, C3-X | Each tier: **LOCK30** (2 s warm-up + 10 s at 30 fps), **OFF** (1 s + 5 s with vsync off = rendering cost), **VERDICT** | FITS_30 / TIGHT_30 / NOT_30 / SKIPPED |
| | C3-D60 | Dungeon M at swap interval 1 | 60 fps diagnostic only; never a fit verdict. Labelled `DIAG_60_OK` when P95 ≤ 17.4 ms, otherwise `DIAG_60_NO` |
| C4 memory | C4-V16/32/48/64 | 1 MiB RGBA8 textures held in VRAM | FITS / STOPPED / SKIPPED; upload MiB/s |
| | C4-H16/32/64/96 | 2 MiB heap chunks written and read back | FITS / STOPPED / SKIPPED |
| | C4-CYC | 20 load/unload cycles of two different area mixes (14 MiB textures + 7 MiB heap), then the largest VRAM tier that fitted is allocated again | NO_DRIFT / DRIFT / REFIT_FAILED |
| | C4-STRM | Dungeon M at 30 fps while a worker streams and inflates the 16 MiB pack and the main thread uploads its textures | NO_HITCHES / HITCHES |
| | C4-SOAK | Battle M at 30 fps for 30 s while holding 48 MiB of VRAM and 64 MiB of heap | STABLE / UNSTABLE |
| C3-T formats | C3-T-RGBA8 / RGB565 / DXT1 / DXT5 / ETC1 / PVRTC4 | Dungeon M with its 8 materials in each format (compressed formats use synthetic blocks and full mip chains). Upload status, VRAM, rendering cost. Run last, because compressed uploads are the least proven step | SUPPORTED / UNSUPPORTED |
| C5 | C5-01 | Every row of every complete iteration compared on id, mode, status, ok and check value. Timing spread is logged but is never a failure | PASS / FAIL / N/A |

### 30 fps rules (primary target)
- **Stable:**
  - at most 1% of frame intervals over **34.0 ms** (33.3 ms plus 0.7 ms timer slack);
  - **P95 ≤ 34.0 ms**;
  - no interval over **51.0 ms**.
- **Classes** (cost = P95 with vsync off):
  - **FITS_30:** stable and cost ≤ 16.7 ms;
  - **TIGHT_30:** stable and 16.7 < cost ≤ 25 ms;
  - **NOT_30:** otherwise.
- **60 fps:** diagnostic only. A tier is marked "60_CANDIDATE" when its vsync-off P95 is ≤ 8.3 ms. C3-D60 runs Dungeon M at swap interval 1 and is labelled `DIAG_60_OK` when its P95 is ≤ 17.4 ms (16.7 ms plus the same 0.7 ms timer allowance as the 30 fps rule).
- **Percentiles** use the nearest-rank method. `tools/analyze_contentfit_results.py` recomputes every value from the raw frame file.
- **Timing basis:** all timings are CPU-side intervals between frame starts. There is no GPU timer.

### Scene tiers, as verified on the PC with the stand-in GL
| Scene | Draws | Triangles | Texture binds | Blended screens | VRAM estimate (KiB) |
|---|---|---|---|---|---|
| Dungeon S / M / L / XL | 20 / 60 / 150 / 300 | 10,000 / 30,000 / 60,000 / 100,800 | 4 / 8 / 16 / 32 | 0 | 1,502 / 3,135 / 22,659 / 45,063 |
| Battle S / M / L / XL | 25 / 28 / 31 / 31 | 12,008 / 25,208 / 50,610 / 71,210 | 9 / 12 / 14 / 14 | 0.251 / 0.300 / 1.398 / 1.545 | 4,915 / 7,662 / 10,392 / 10,392 |
| Dialogue S / M / L / XL | 3 / 4 / 5 / 6 | 6 / 8 / 10 / 12 | 3 / 4 / 5 / 6 | 0.547 / 0.847 / 1.147 / 1.447 | 6,208 / 8,256 / 10,304 / 12,352 |
| Menu S / M / L / XL | 2 / 3 / 4 / 7 | 1,002 / 3,002 / 6,002 / 12,002 | 1 | 0.138 / 0.414 / 0.827 / 1.654 | 256 |
| Combined | 31 | 25,214 | 15 | 1.147 | 11,822 |
| Formats (dungeon M): RGBA8 / 565 / DXT1 / DXT5 / ETC1 / PVRTC4 | 60 | 30,000 | 8 | 0 | 3,135 / 1,770 / 746 / 1,087 / 746 / 747 |

Notes on the table:
- Blended screens = blended triangle area ÷ (960 × 544), not clipped to the screen.
- VRAM = texture bytes for each format, including mip chains, plus vertex and index buffers.
- The PC check confirms these counts on 6 different frames per tier, that no dungeon triangle faces away from the camera, that the VRAM estimate equals the bytes uploaded, and that the heap estimate covers the measured peak.

## Memory limits
**Margins:**
- Video memory: at least 16 MiB free.
- vitaGL RAM pool: at least 16 MiB free.
- C library heap: at least 24 MiB unallocated.
- Kernel free memory: at least 8 MiB (thread stacks come from here).
- Single allocation: at most 8 MiB.

**The pre-check:** a step runs only if, for every pool it needs,
`free_now − margin ≥ need × 1.01` (computed in integers as `free ≥ margin + need + ceil(need/100)`).

**Safeguards:**
- **Unknown size:** a pool whose size cannot be measured is never used (`UNKNOWN_SIZE`). For example, if the heap block-size query fails, every heap step is skipped.
- **During allocation:** after every chunk, the step stops (and records `MARGIN` / `SPILL`) if a pool falls below its margin or if memory lands in a vitaGL pool other than the intended one.
- **After freeing:** memory must return to within 64 KiB of its starting level. In C4, if it doesn't, the remaining memory steps are recorded as `NOT_RUN` instead of running on a dirty baseline.

**Expected outcomes** (inferred before the run from VitaBench's measured pools) **and measured outcomes** (run 001; the same in all 3 iterations):

| Tier | Expected outcome | Measured (run 001) |
|---|---|---|
| VRAM 16 / 32 / 48 MiB | Runs | Fitted; 67.9 / 51.9 / 35.9 MiB left |
| VRAM 64 MiB | Borderline: it needs 80.6 MiB free, and about 82.9 MiB was free in VitaBench | Fitted; 83.9 MiB was free at the start, 19.9 MiB left |
| Heap 16 / 32 / 64 MiB | Runs | Fitted |
| Heap 96 MiB | Runs only if the heap's starting usage is 7.0 MiB or less (expected 1–2 MiB) | Fitted; heap in use at each iteration's start was 57,888–66,096 bytes |

## Synthetic fixtures
`tools/make_contentfit_fixtures.py` writes `contentfit/fixtures/` (git-ignored; deterministic). It generates into a staging folder, checks there, and only then moves the files into place.

| File | Contents |
|---|---|
| `io16.bin` | 16 MiB of incompressible bytes |
| `pack08.gdat` … `pack64.gdat` | 25 / 43 / 95 / 178 entries; 8 / 16 / 32 / 64 MiB of payload; 2.7 / 5.4 / 9.7 / 20.0 MB on disk (ratio 3.1–3.5) |

**Pack format:**
- The GDAT index layout, the 128-byte header with `BILZ` at 0x80, and zlib streams that GDAT Proto's reader accepts in strict mode.
- Each entry's payload is ContentFit's own `CFP1` format: a 32-byte header (kind, width, height, body length, body CRC, entry index) followed by an RGBA8 texture or a data blob.
- Pack08's entry 0 is the 512×1024 portrait.
- About 60% of each pack's payload bytes are textures.

**Checks:** the expected values are compiled into the app (`cf_fixtures.h`). `tools/check_contentfit_fixtures.py` re-reads everything independently and must agree with both `cf_fixtures.h` and `expected.json`.

## Output files
| File | Contents |
|---|---|
| `runNNN.csv` | One row per measurement: `iteration,id,name,mode,status,ok,verdict,frames,avg_ms,p50_ms,p95_ms,p99_ms,max_ms,over_16_7,over_34,over_51,ms,mib,mib_per_s,vram_kib,glram_kib,heap_kib,check,detail` |
| `runNNN_frames.csv` | Every measured frame interval (`iteration,id,mode,frame,ms`), for independent recomputation |
| `runNNN.log` | Environment and pools, every row in readable form, timing spread across iterations, a verdict summary, allocator counters, and any SDL log messages |
| `runNNN_progress.txt` | One line before each step starts |

Every file is flushed after each write, so a crash keeps everything written up to that point.

## PC validation (2026-11-15)
| Check | Result |
|---|---|
| Fixture generator + independent checker | All checks pass: 4 packs, 341 entries, every inflate exact, every CRC matches the header and JSON |
| Every ContentFit source, host gcc, `-Wall -Wextra -Werror` at `-O1` / `-O2` / `-O3` | Clean |
| Unit tests: statistics, memory pre-check (exact boundaries, the 96 MiB tier, unknown pools, guard, spill, baseline), paths and no-overwrite, shader-cache path rewrite, `cf_zinflate` vs Phase 8 `rp_inflate` on all 341 entries and damaged streams, payload checks, ring reader, deterministic content | 3,181 checks, 0 failures under AddressSanitizer + UndefinedBehaviorSanitizer; 0 failures and 0 reports under ThreadSanitizer |
| Shader-cache redirect: the unchanged `cf_iowrap.c` linked with `--wrap=sceIoOpen,--wrap=sceIoMkdir` against a stand-in making vitaGL's exact calls | 14 checks, 0 failures: the 4 folder creations and both cache-file formats arrive below `ux0:data/ContentFit/shader_cache`; nothing reaches `ux0:data/shader_cache`; other paths, including `ux0:data/shader_cachex`, pass through unchanged; an over-long cache path is refused with no call made |
| Statistics against the independent Python implementation | 40 random sets, 0 disagreements |
| Scene check (stand-in GL) | 718 checks, 0 failures: the table above |
| Whole-suite dry runs (stand-in GL and platform; host timings are not measurements) | All five modes pass: full (3 iterations, C5 PASS); quick with every-second-vblank pacing; quick with the heap size unknown (heap steps skipped); quick with 10 MiB of heap already used (96 MiB tier skipped, 64 MiB runs); stopped part-way by START. No tracked bytes, GL objects or invalid GL calls left; the analyzer recomputes every run with 0 disagreements; `run001.csv` is unchanged after four later runs |
| Test runner | Fails when the build fails, including when only the sanitizer builds or only the `--wrap` link fail |
| Vita build (`build_contentfit.sh`, and again from scratch) | 27 C files, 0 warnings, 0 errors |
| VPK package checks | 68/68 pass. Title ID VDNG00005; only the 5 allowed fixtures; no immediate-mode calls; 0 strong undefined symbols; no other app's output folder in the binary; `Mary`, `Skelter` and `Finale` appear only in ContentFit's disclaimers. In the linked Vita program, the real `sceIoOpen` and `sceIoMkdir` are called only from the two wrappers; vitaGL's `reload_ffp_shaders` and `vglInitWithCustomSizes` call the wrappers |

## Hardware results (run 001)
Run on 2026-11-15: 3 iterations in 1,513.4 s, all 289 result rows `OK`. The full report, with every figure, is `CONTENTFIT_RESULTS.md`. The run files (`run001.csv`, `run001.log`, `run001_frames.csv`, `run001_progress.txt`) are not stored in this repository.

| Check | Result |
|---|---|
| Independent recomputation (`tools/analyze_contentfit_results.py`) | 184 checks, 0 disagreements |
| Repeatability (C5-01) | PASS: 3 × 96 rows compared, 0 differences |
| Tracked memory | 7,626 allocations, 7,626 frees, 0 refused, 0 bytes left at the end |
| 30 fps (C3) | All 17 scene tiers stable and `FITS_30` in every iteration; locked P95 33.41–33.42 ms; no interval over 34.0 ms. Heaviest: Battle XL, cost P95 16.27–16.31 ms |
| Pacing (C3-P) | Swap interval 2 honoured: median 33.365–33.366 ms |
| 60 fps diagnostic (C3-D60) | `DIAG_60_OK` in every iteration: median 16.68 ms, longest interval 16.74–16.78 ms, no missed refresh |
| Storage (C1) | From `app0:`: stdio 2.56–2.58 MiB/s at every chunk size, direct `sceIoRead` 9.34–9.73 MiB/s. From ux0: stdio 2.86–2.87 MiB/s, direct 9.81–9.82 MiB/s. 16 KiB reads: 1.63 ms each in order, 2.93–2.94 ms shuffled. One-time copy to ux0: 1.77 MiB/s |
| Loads (C2) | Pipelined: 0.63–0.64 s (8 MiB), 2.47–2.48 s (32 MiB). Serial: 1.52 s and 5.66–5.67 s. Both targets met either way. Portrait: 0.43 s |
| Memory (C4) | Every VRAM and heap tier fitted (table above); 20 cycles with 0 KiB drift; streaming `NO_HITCHES`; 30 s soak `STABLE` |
| Formats (C3-T) | All six accepted at every mip level (`SUPPORTED`); cost P95 5.65–5.71 ms |
| Shader cache | Redirected: 4 folder creations and 10 file opens, 0 refused |

**Answered by the run:**
- vitaGL's shader-cache calls are redirected on the device (counts above). The log counts redirected calls, not their results; that `ux0:data/ContentFit/shader_cache/` exists on the Vita can be confirmed in VitaShell.
- Swap interval 2 is honoured, so every locked run used it rather than the every-second-vblank fallback.
- All six formats upload at every mip level.
- The heap block-size query works on the device: 134,217,728 bytes (128 MiB), so the heap steps ran.
- The real timings and memory figures: above, and in full in `CONTENTFIT_RESULTS.md`.

**Still not shown:**
- Whether the compressed formats decode to the correct image. The test blocks were random data.
- GPU time. Every timing is a CPU-side interval between frame starts.
- Free space on ux0. The query (`sceIoDevctl`) failed on the device and was logged as -1; with the size unknown, the one-time copy went ahead without a free-space check.
- Anything about Mary Skelter Finale. These results describe this Vita and this stack only.
