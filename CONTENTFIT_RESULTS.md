# Vita ContentFit --- Hardware Results (Run 001)

**Run date:** 2026-11-15\
**Application:** Vita ContentFit (`VDNG00005`)\
**Test platform:** PlayStation Vita\
**Graphics stack:** SDL2 + vitaGL\
**Iterations:** 3\
**Run duration:** 1,513.4 seconds (25 minutes, 13.4 seconds)\
**Result:** PASS --- 289 of 289 recorded result rows reported `OK`

## 1. Scope and interpretation

This run measures the Vita hardware and this project's SDL2 + vitaGL
test application using synthetic content. **No Mary Skelter Finale files
were read.** The results establish a hardware and runtime baseline; they
do not demonstrate that Finale itself has been ported, that its original
assets are compatible, or that its complete game workload will meet
these targets.

The primary performance target was stable 30 FPS: - No more than 1% of
measured frame intervals above 34.0 ms. - P95 frame interval no greater
than 34.0 ms. - No measured frame interval above 51.0 ms.

The 60 FPS measurements were diagnostic only.

## 2. Overall result

  Iteration     Result rows   Passed (`OK`)
  ----------- ------------- ---------------
  1                      96              96
  2                      96              96
  3                      97              97
  **Total**         **289**         **289**

All recorded result rows reported `OK`. The progress log records
completion after 1,513.4 seconds. The three iterations produced
consistent scene verdicts and content checksums.

## 3. Test environment

At the start of the run: - ARM: 333 MHz - Bus: 222 MHz - GPU: 111 MHz -
GPU crossbar: 111 MHz - Battery: 99%, not charging - vitaGL VRAM pool:
96 MiB total, 83.9 MiB free - vitaGL RAM pool: 100 MiB total, 63.5 MiB
free - vitaGL PHYCONT pool: 26 MiB total, approximately 26 MiB free - C
library heap block: 128 MiB, approximately 127.9 MiB free at the initial
snapshot - Kernel USER_RW free memory: 12 MiB

These are snapshots reported by the test application and its
memory-query methods. Pool values are not interchangeable and should not
be added together as one universal free-memory figure.

## 4. Graphics and frame pacing

The application tested 17 synthetic scene configurations: four dungeon
tiers, four battle tiers, four dialogue tiers, four menu tiers, and one
combined scene. All 17 configurations received `FITS_30` verdicts in
each of the three iterations.

Average P95 rendering cost across the three iterations, measured with
swap interval disabled to estimate rendering cost:

  Scene             P95 rendering cost
  --------------- --------------------
  BATTLE_L                    10.54 ms
  BATTLE_M                     6.75 ms
  BATTLE_S                     6.30 ms
  BATTLE_XL                   16.30 ms
  COMBINED                     8.94 ms
  DIALOGUE_L                   4.78 ms
  DIALOGUE_M                   3.94 ms
  DIALOGUE_S                   3.13 ms
  DIALOGUE_XL                  5.61 ms
  DUNGEON_L                    6.05 ms
  DUNGEON_M                    5.70 ms
  DUNGEON_S                    5.31 ms
  DUNGEON_XL                   7.45 ms
  FORMAT_DXT1                  5.66 ms
  FORMAT_DXT5                  5.67 ms
  FORMAT_ETC1                  5.66 ms
  FORMAT_PVRTC4                5.66 ms
  FORMAT_RGB565                5.68 ms
  FORMAT_RGBA8                 5.71 ms
  MENU_L                       3.98 ms
  MENU_M                       2.75 ms
  MENU_S                       2.02 ms
  MENU_XL                      7.16 ms

The most demanding scene was `BATTLE_XL`, averaging approximately 16.30
ms P95. The combined scene averaged approximately 8.94 ms P95. These are
synthetic benchmark scenes, not captures of Finale gameplay.

### 30 FPS pacing

The test confirmed that SDL's swap interval 2 produced approximately
33.37 ms frame intervals. The locked 30 FPS scene runs remained
stable: - No intervals above 34 ms in the reported 300-frame scene
samples. - No intervals above 51 ms. - P95 locked frame intervals were
approximately 33.41--33.42 ms.

The separate 60 FPS diagnostic was marginally over the 16.7 ms frame
budget: P95 was approximately 16.73--16.74 ms, and 61--74 of 300
intervals per iteration exceeded 16.7 ms. This supports prioritizing 30
FPS rather than treating 60 FPS as a target.

## 5. Storage and file I/O

The benchmark read a 16 MiB synthetic file and verified matching CRC
values.

Representative average results across iterations:

  Method                                  Approximate throughput
  ------------------------------------- ------------------------
  Standard I/O, 16 KiB--4 MiB buffers           2.56--2.57 MiB/s
  Raw Vita I/O, 64 KiB--4 MiB buffers           9.35--9.73 MiB/s
  Raw Vita I/O from UX0, 1 MiB buffer           9.81--9.82 MiB/s
  Standard I/O from UX0, 1 MiB buffer           2.86--2.87 MiB/s
  Sequential 512 × 16 KiB reads                       9.61 MiB/s
  Random 512 × 16 KiB reads                     5.32--5.33 MiB/s

The raw I/O path was substantially faster than the standard I/O path in
this test. Random small reads were slower than sequential reads, which
makes archive access patterns and read batching important design
considerations.

## 6. Synthetic archive loading

The test compared serial loading with a pipelined read/inflate path.
Mean elapsed times across the three iterations:

| Logical content size \| Serial \| Pipelined \|
| 8 MiB \| 1.52 s \| 0.63 s \|
| 16 MiB \| 3.04 s \| 1.12 s \|
| 32 MiB \| 5.66 s \| 2.48 s \|
| 64 MiB \| 11.53 s \| 4.86 s \|

The 8 MiB battle-set load completed in approximately 0.63--0.64 seconds
using the pipelined path, below the test's 2-second target. The 32 MiB
area load completed in approximately 2.47 seconds pipelined, below the
10-second target. The 64 MiB pack took approximately 4.85--4.87 seconds
pipelined.

The 2 MiB portrait swap completed in approximately 0.43 seconds, meeting
its 0.5-second target.

These figures include the benchmark's synthetic archive layout,
compression ratios, and workload. They should not be treated as
guaranteed Finale loading times.

## 7. Memory and repeated loading

The following synthetic allocation tests passed: - Resident RGBA8
textures at 16, 32, 48, and 64 MiB. - Heap working sets at 16, 32, 64,
and 96 MiB. - 20 repeated area load/unload cycles, with no measured
drift in the tracked VRAM, GL RAM, or heap counters. - A 30-second
locked-30-FPS soak test using the medium battle scene while holding 48
MiB of textures and a 64 MiB heap working set.

The streaming test processed 228 entries and uploaded 90 textures while
measuring 300 locked frame intervals. It reported no errors and no
intervals above 34 ms. Average texture upload time was 2.09 ms; the
worst reported upload was 6.66 ms.

The test's allocation guards and tracked counters are evidence about
these specific benchmark allocations. They do not prove the complete
game can safely use the same memory budget.

## 8. Texture format checks

The test reported all six tested formats as supported in its rendering
path:

  Format   Result
  -------- -----------
  RGBA8    Supported
  RGB565   Supported
  DXT1     Supported
  DXT5     Supported
  ETC1     Supported
  PVRTC4   Supported

For the tested 8 × 256² texture scene with mipmaps, measured texture
storage ranged from approximately 341 KiB for DXT1/ETC1 to approximately
2,731 KiB for RGBA8. These tests establish format support in this
application; they do not identify the formats used by Finale's original
assets.

## 9. Shader-cache path redirection

The runtime log confirms that vitaGL shader-cache paths were redirected
to:

`ux0:data/ContentFit/shader_cache`

During the run, the application reported four folder creations and four
file opens redirected from `ux0:data/shader_cache`, with zero refused
paths. This verifies the tested redirect behavior on the device for the
operations observed in this run.

## 10. Engineering implications

The strongest results for the next development stage are:

1.  **Keep 30 FPS as the primary target.** All tested synthetic scenes
    met the benchmark's 30 FPS criteria. The 60 FPS diagnostic did not
    consistently meet a 16.7 ms interval.
2.  **Prefer the raw Vita file-I/O path where appropriate.** It was
    substantially faster than standard I/O in these tests.
3.  **Design for pipelined reads and decompression.** The synthetic
    archive pipeline materially reduced load times compared with serial
    processing.
4.  **Avoid excessive random small reads.** Sequential 16 KiB reads were
    faster than random reads.
5.  **Budget memory by pool and workload.** VRAM, GL RAM, heap, and
    kernel memory must be tracked separately.
6.  **Retain texture-format flexibility.** Several formats passed the
    prototype's checks, but actual game asset formats still need to be
    established.

## 11. Limitations and next validation

This run does not establish: - Compatibility with Finale's original
archive files or proprietary resource formats. - Compatibility with
Finale's original shaders, materials, animation, or rendering effects. -
Performance of Finale's actual scenes, characters, effects, UI, or
battle logic. - Full-game memory use, audio/video playback, save
behavior, or long-session stability. - Whether the game's assets can be
legally or technically used in a Vita adaptation.

The next technical validation should connect the tested file-I/O,
decompression, and texture-upload path to the separately developed
synthetic GDAT/resource-loader prototype. Any later test involving
actual game content must be separately authorized and documented.

## 12. Source files

This report was prepared from the Run 001 result CSV, frame-interval
CSV, runtime log, and progress log: - `run001.csv` -
`run001_frames.csv` - `run001.log` - `run001_progress.txt`

**Conclusion:** ContentFit Run 001 passed on the tested Vita. The
results provide a useful measured baseline for a 30 FPS-oriented Vita
runtime and resource-loading pipeline. They are encouraging engineering
evidence, not proof that Mary Skelter Finale itself will run on the
hardware.
