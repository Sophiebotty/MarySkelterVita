# VitaBench Hardware Benchmark Results

**Date analysed:** 2026-11-14  
**Hardware:** the project owner's modded PS Vita  
**Benchmark:** VitaBench (`bench/`, title ID `VDNG00002`): original synthetic content only  
**Repository state at analysis:** `12270f2` (VitaBench sources not yet committed)

### Source data (read directly; values copied without rounding)
| Build | File | Size | SHA-256 (first 16) | CSV header |
|---|---|---|---|---|
| MSAA off | `benchmark-data/results_msaa0.csv` | 3,345 B | `e3e031fcea8183f8` | `build MSAA=0, swap_interval=0 (0 = vsync off)` |
| MSAA 2x | `benchmark-data/results_msaa2.csv` | 3,339 B | `5fc557be406d2875` | `build MSAA=2, swap_interval=0` |
| MSAA 4x | `benchmark-data/results_msaa4.csv` | 3,339 B | `e2f1a3102bf7280f` | `build MSAA=4, swap_interval=0` |

The CSVs are byte-identical copies (same SHA-256) of the files copied off the Vita (`ux0:data/VitaBench/`), stored unmodified in `benchmark-data/`.

Each file has all **25 rows**. All three runs had **vsync off** (swap interval 0, as reported by SDL).

### How to read this document
| Marker | Meaning |
|---|---|
| **MEASURED** | A value copied exactly from a CSV, or simple arithmetic on CSV values (a difference or ratio), shown with the operands |
| **OBSERVATION** | A pattern visible in the measured values |
| **RECOMMENDATION (provisional)** | Engineering advice derived from the measurements; not a measurement |

> **Scope warning.** These are **synthetic** tests: a procedural triangle grid, full-screen textured quads and procedural textures in an orthographic view. They measure isolated costs of our SDL2 + vitaGL stack on this Vita. **They do not predict how Mary Skelter Finale, or any real game scene, would perform.** Real scenes mix geometry, overdraw, blending, shaders, animation, audio and game logic in ways these tests don't.

### Measurement method (from the VitaBench source)
- **Frame time:** the interval between consecutive frame starts (`SDL_GetPerformanceCounter`) on the CPU side. There are no GPU timers.
- **Measurement window:** 1 s warm-up, then **5 s** of measurement per test. **One run per build.**
- **P95:** the sorted sample at index ⌊0.95·n⌋.
- **Overlay included:** every frame includes clear, depth test and the small overlay drawn by VitaBench, so test 1 (EMPTY) is the fixed per-frame baseline.
- **Draw path:** triangle and draw-call tests use VBOs unless noted. Fill tests draw full-screen quads from client arrays at about 1 texel per pixel.
- **Free memory:** "free RAM/VRAM" is `vglMemFree(VGL_MEM_RAM / VGL_MEM_VRAM)`. These are vitaGL's memory pools, not total system memory.

---

## 1. All 25 tests across MSAA off, 2x and 4x: average frame time and FPS (MEASURED)

"2x vs OFF" and "4x vs OFF" are differences in average milliseconds; negative means faster than MSAA off.

| ID | Test | Variant | Avg ms OFF | Avg ms 2x | Avg ms 4x | FPS OFF | FPS 2x | FPS 4x | 2x vs OFF | 4x vs OFF |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | EMPTY | CLEAR SWAP | 1.778 | 1.931 | 2.307 | 562.52 | 517.86 | 433.47 | +0.153 | +0.529 |
| 2 | TRIS | 10K FLAT | 2.084 | 2.310 | 2.613 | 479.91 | 432.82 | 382.68 | +0.226 | +0.529 |
| 3 | TRIS | 50K FLAT | 3.858 | 3.549 | 4.007 | 259.20 | 281.76 | 249.59 | -0.309 | +0.149 |
| 4 | TRIS | 100K FLAT | 6.515 | 5.502 | 5.588 | 153.50 | 181.76 | 178.96 | -1.013 | -0.927 |
| 5 | TRIS | 200K FLAT | 11.242 | 8.537 | 8.929 | 88.95 | 117.14 | 111.99 | -2.705 | -2.313 |
| 6 | TRIS | 50K TEX | 4.030 | 3.929 | 4.362 | 248.14 | 254.53 | 229.25 | -0.101 | +0.332 |
| 7 | TRIS | 200K TEX | 11.860 | 9.577 | 9.984 | 84.31 | 104.41 | 100.16 | -2.283 | -1.876 |
| 8 | DRAWS | 10 X 5000 | 3.870 | 3.566 | 3.980 | 258.37 | 280.41 | 251.26 | -0.304 | +0.110 |
| 9 | DRAWS | 100 X 500 | 3.838 | 3.595 | 4.034 | 260.59 | 278.15 | 247.90 | -0.243 | +0.196 |
| 10 | DRAWS | 500 X 100 | 3.813 | 3.747 | 4.081 | 262.26 | 266.85 | 245.03 | -0.066 | +0.268 |
| 11 | DRAWS | 1000 X 50 | 4.938 | 5.073 | 5.399 | 202.49 | 197.10 | 185.21 | +0.135 | +0.461 |
| 12 | FILL | 1 LAYER | 1.972 | 2.102 | 2.707 | 506.98 | 475.64 | 369.39 | +0.130 | +0.735 |
| 13 | FILL | 2 LAYERS | 2.008 | 2.183 | 2.926 | 498.08 | 458.00 | 341.74 | +0.175 | +0.918 |
| 14 | FILL | 4 LAYERS | 2.066 | 2.421 | 3.335 | 483.92 | 413.06 | 299.88 | +0.355 | +1.269 |
| 15 | FILL | 8 LAYERS | 2.307 | 2.927 | 4.161 | 433.55 | 341.63 | 240.34 | +0.620 | +1.854 |
| 16 | BLEND | 4 LAYERS | 7.221 | 8.161 | 8.601 | 138.49 | 122.53 | 116.27 | +0.940 | +1.380 |
| 17 | BLEND | 8 LAYERS | 13.289 | 15.239 | 15.386 | 75.25 | 65.62 | 64.99 | +1.950 | +2.097 |
| 18 | PATH | CLIENT 50K FLAT | 7.379 | 7.426 | 7.560 | 135.51 | 134.67 | 132.28 | +0.047 | +0.181 |
| 19 | PATH | VBO 50K FLAT | 3.862 | 3.580 | 4.040 | 258.93 | 279.30 | 247.54 | -0.282 | +0.178 |
| 20 | TEX565 | FILL 4 LAYERS | 2.005 | 2.288 | 3.234 | 498.81 | 436.99 | 309.24 | +0.283 | +1.229 |
| 21 | TEX565 | FILL 8 LAYERS | 2.128 | 2.782 | 4.079 | 469.89 | 359.39 | 245.16 | +0.654 | +1.951 |
| 22 | TEX565 | 200K TEX | 11.840 | 9.567 | 9.962 | 84.46 | 104.53 | 100.38 | -2.273 | -1.878 |
| 23 | FOG | FILL 4 LAYERS | 3.893 | 3.947 | 4.269 | 256.86 | 253.36 | 234.27 | +0.054 | +0.376 |
| 24 | FOG | 200K TEX | 13.480 | 10.345 | 10.206 | 74.18 | 96.67 | 97.98 | -3.135 | -3.274 |
| 25 | TEXMEM | ALLOC 1MB STEPS | — | — | — | — | — | — | (not timed; see §6) | |

---

## 2. Average, minimum, maximum and 95th-percentile frame times (MEASURED)

| ID | Test | Variant | Tris | Draws | MSAA | Frames | Avg ms | Min ms | Max ms | P95 ms | Avg FPS |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | EMPTY | CLEAR SWAP | 0 | 0 | off | 2813 | 1.778 | 1.415 | 2.043 | 1.883 | 562.52 |
| | | | | | 2x | 2590 | 1.931 | 1.719 | 2.312 | 2.086 | 517.86 |
| | | | | | 4x | 2168 | 2.307 | 1.884 | 2.480 | 2.371 | 433.47 |
| 2 | TRIS | 10K FLAT | 10000 | 1 | off | 2400 | 2.084 | 1.800 | 2.626 | 2.418 | 479.91 |
| | | | | | 2x | 2165 | 2.310 | 1.899 | 2.658 | 2.463 | 432.82 |
| | | | | | 4x | 1914 | 2.613 | 2.209 | 2.899 | 2.682 | 382.68 |
| 3 | TRIS | 50K FLAT | 50000 | 2 | off | 1297 | 3.858 | 3.568 | 4.448 | 4.088 | 259.20 |
| | | | | | 2x | 1409 | 3.549 | 3.357 | 4.143 | 3.660 | 281.76 |
| | | | | | 4x | 1248 | 4.007 | 3.621 | 4.502 | 4.388 | 249.59 |
| 4 | TRIS | 100K FLAT | 100000 | 4 | off | 768 | 6.515 | 5.794 | 7.298 | 7.014 | 153.50 |
| | | | | | 2x | 909 | 5.502 | 5.289 | 5.906 | 5.638 | 181.76 |
| | | | | | 4x | 895 | 5.588 | 5.224 | 6.065 | 5.958 | 178.96 |
| 5 | TRIS | 200K FLAT | 200000 | 7 | off | 445 | 11.242 | 9.730 | 12.714 | 12.055 | 88.95 |
| | | | | | 2x | 586 | 8.537 | 8.140 | 9.132 | 8.844 | 117.14 |
| | | | | | 4x | 560 | 8.929 | 8.727 | 9.208 | 9.074 | 111.99 |
| 6 | TRIS | 50K TEX | 50000 | 2 | off | 1241 | 4.030 | 3.755 | 4.659 | 4.269 | 248.14 |
| | | | | | 2x | 1273 | 3.929 | 3.517 | 4.377 | 4.226 | 254.53 |
| | | | | | 4x | 1147 | 4.362 | 3.901 | 4.675 | 4.475 | 229.25 |
| 7 | TRIS | 200K TEX | 200000 | 7 | off | 422 | 11.860 | 10.040 | 13.229 | 12.642 | 84.31 |
| | | | | | 2x | 523 | 9.577 | 8.877 | 10.347 | 10.009 | 104.41 |
| | | | | | 4x | 501 | 9.984 | 9.601 | 10.661 | 10.435 | 100.16 |
| 8 | DRAWS | 10 X 5000 | 50000 | 10 | off | 1292 | 3.870 | 3.558 | 4.295 | 4.108 | 258.37 |
| | | | | | 2x | 1403 | 3.566 | 3.283 | 4.062 | 3.916 | 280.41 |
| | | | | | 4x | 1257 | 3.980 | 3.492 | 4.421 | 4.329 | 251.26 |
| 9 | DRAWS | 100 X 500 | 50000 | 100 | off | 1303 | 3.838 | 3.559 | 4.423 | 4.074 | 260.59 |
| | | | | | 2x | 1391 | 3.595 | 3.354 | 4.157 | 3.951 | 278.15 |
| | | | | | 4x | 1240 | 4.034 | 3.618 | 4.513 | 4.418 | 247.90 |
| 10 | DRAWS | 500 X 100 | 50000 | 500 | off | 1312 | 3.813 | 3.562 | 4.408 | 4.033 | 262.26 |
| | | | | | 2x | 1335 | 3.747 | 3.309 | 4.103 | 4.005 | 266.85 |
| | | | | | 4x | 1226 | 4.081 | 3.670 | 4.463 | 4.164 | 245.03 |
| 11 | DRAWS | 1000 X 50 | 50000 | 1000 | off | 1013 | 4.938 | 4.273 | 5.477 | 5.205 | 202.49 |
| | | | | | 2x | 986 | 5.073 | 4.829 | 5.300 | 5.198 | 197.10 |
| | | | | | 4x | 927 | 5.399 | 5.183 | 5.549 | 5.465 | 185.21 |
| 12 | FILL | 1 LAYER | 2 | 1 | off | 2536 | 1.972 | 1.711 | 2.410 | 2.300 | 506.98 |
| | | | | | 2x | 2379 | 2.102 | 1.735 | 2.496 | 2.415 | 475.64 |
| | | | | | 4x | 1847 | 2.707 | 2.475 | 2.910 | 2.812 | 369.39 |
| 13 | FILL | 2 LAYERS | 4 | 2 | off | 2491 | 2.008 | 1.796 | 2.375 | 2.297 | 498.08 |
| | | | | | 2x | 2290 | 2.183 | 1.818 | 2.534 | 2.266 | 458.00 |
| | | | | | 4x | 1709 | 2.926 | 2.699 | 3.085 | 3.001 | 341.74 |
| 14 | FILL | 4 LAYERS | 8 | 4 | off | 2420 | 2.066 | 1.775 | 2.420 | 2.295 | 483.92 |
| | | | | | 2x | 2066 | 2.421 | 2.231 | 2.619 | 2.502 | 413.06 |
| | | | | | 4x | 1500 | 3.335 | 3.140 | 3.512 | 3.403 | 299.88 |
| 15 | FILL | 8 LAYERS | 16 | 8 | off | 2168 | 2.307 | 2.099 | 2.498 | 2.376 | 433.55 |
| | | | | | 2x | 1709 | 2.927 | 2.743 | 3.149 | 3.008 | 341.63 |
| | | | | | 4x | 1202 | 4.161 | 4.000 | 4.342 | 4.229 | 240.34 |
| 16 | BLEND | 4 LAYERS | 8 | 4 | off | 693 | 7.221 | 7.078 | 7.357 | 7.264 | 138.49 |
| | | | | | 2x | 613 | 8.161 | 8.043 | 8.287 | 8.197 | 122.53 |
| | | | | | 4x | 582 | 8.601 | 8.496 | 8.715 | 8.639 | 116.27 |
| 17 | BLEND | 8 LAYERS | 16 | 8 | off | 377 | 13.289 | 13.158 | 13.394 | 13.353 | 75.25 |
| | | | | | 2x | 329 | 15.239 | 15.116 | 15.337 | 15.283 | 65.62 |
| | | | | | 4x | 325 | 15.386 | 15.307 | 15.493 | 15.430 | 64.99 |
| 18 | PATH | CLIENT 50K FLAT | 50000 | 2 | off | 678 | 7.379 | 6.152 | 7.958 | 7.783 | 135.51 |
| | | | | | 2x | 674 | 7.426 | 6.384 | 8.050 | 7.778 | 134.67 |
| | | | | | 4x | 662 | 7.560 | 6.496 | 8.266 | 7.971 | 132.28 |
| 19 | PATH | VBO 50K FLAT | 50000 | 2 | off | 1295 | 3.862 | 3.605 | 4.531 | 4.091 | 258.93 |
| | | | | | 2x | 1397 | 3.580 | 3.375 | 4.140 | 3.890 | 279.30 |
| | | | | | 4x | 1238 | 4.040 | 3.623 | 4.553 | 4.439 | 247.54 |
| 20 | TEX565 | FILL 4 LAYERS | 8 | 4 | off | 2495 | 2.005 | 1.850 | 2.357 | 2.066 | 498.81 |
| | | | | | 2x | 2185 | 2.288 | 2.095 | 2.484 | 2.362 | 436.99 |
| | | | | | 4x | 1547 | 3.234 | 3.107 | 3.389 | 3.290 | 309.24 |
| 21 | TEX565 | FILL 8 LAYERS | 16 | 8 | off | 2350 | 2.128 | 1.864 | 2.473 | 2.265 | 469.89 |
| | | | | | 2x | 1797 | 2.782 | 2.626 | 2.914 | 2.844 | 359.39 |
| | | | | | 4x | 1226 | 4.079 | 3.920 | 4.228 | 4.146 | 245.16 |
| 22 | TEX565 | 200K TEX | 200000 | 7 | off | 423 | 11.840 | 10.321 | 13.119 | 12.711 | 84.46 |
| | | | | | 2x | 523 | 9.567 | 8.875 | 10.306 | 10.014 | 104.53 |
| | | | | | 4x | 502 | 9.962 | 9.554 | 10.627 | 10.435 | 100.38 |
| 23 | FOG | FILL 4 LAYERS | 8 | 4 | off | 1285 | 3.893 | 3.729 | 4.055 | 3.929 | 256.86 |
| | | | | | 2x | 1267 | 3.947 | 3.809 | 4.091 | 3.983 | 253.36 |
| | | | | | 4x | 1172 | 4.269 | 4.095 | 4.438 | 4.317 | 234.27 |
| 24 | FOG | 200K TEX | 200000 | 7 | off | 371 | 13.480 | 11.777 | 15.102 | 14.439 | 74.18 |
| | | | | | 2x | 484 | 10.345 | 9.518 | 11.196 | 10.830 | 96.67 |
| | | | | | 4x | 490 | 10.206 | 9.818 | 10.633 | 10.465 | 97.98 |

### Frame-time spread: largest max − min (MEASURED)
| Build | Largest spreads (test: max − min ms) |
|---|---|
| MSAA off | 24: 3.325 · 7: 3.189 · 5: 2.984 · 22: 2.798 · 18: 1.806 |
| MSAA 2x | 24: 1.678 · 18: 1.666 · 7: 1.470 · 22: 1.431 · 5: 0.992 |
| MSAA 4x | 18: 1.770 · 22: 1.073 · 7: 1.060 · 19: 0.930 · 8: 0.929 |

**OBSERVATION:** the heaviest geometry tests in the **MSAA-off run** have the widest spread (about 3 ms), roughly twice the MSAA runs. That suggests conditions in that run varied more; see §11.

---

## 3. Average FPS comparison (MEASURED)
Average FPS for each test and build is in the §1 table. Highlights:
- **Empty frame:** 562.52 (off), 517.86 (2x), 433.47 (4x) FPS.
- **Heaviest geometry (200K textured, test 7):** 84.31 (off), 104.41 (2x), 100.16 (4x) FPS. All are above 60 FPS in this synthetic, small-screen-area test.
- **Heaviest overall (8 blended full-screen layers, test 17):** 75.25 (off), 65.62 (2x), 64.99 (4x) FPS.
- **Below 60 FPS:** no test fell below 60 FPS on average. The slowest average frame time was 15.386 ms (test 17, 4x).

---

## 4. VBO vs client arrays: 50K flat triangles, 2 draws (MEASURED)
| Build | Client arrays (test 18) avg ms | VBO (test 19) avg ms | Difference | Client ÷ VBO |
|---|---|---|---|---|
| MSAA off | 7.379 | 3.862 | 3.517 ms | 1.911 |
| MSAA 2x | 7.426 | 3.580 | 3.846 ms | 2.074 |
| MSAA 4x | 7.560 | 4.040 | 3.520 ms | 1.871 |

**OBSERVATION:** VBOs were faster in all three builds, by 3.517–3.846 ms, with client arrays taking about 1.87–2.07× as long. This is the most consistent result in the data set. Client-array frames also varied more (max − min 1.806 / 1.666 / 1.770 ms).

---

## 5. RGBA8 vs RGB565 (MEASURED)
| Comparison | MSAA off | MSAA 2x | MSAA 4x |
|---|---|---|---|
| Fill 4 layers: RGB565 (20) vs RGBA8 (14) | 2.005 vs 2.066 (−0.061) | 2.288 vs 2.421 (−0.133) | 3.234 vs 3.335 (−0.101) |
| Fill 8 layers: RGB565 (21) vs RGBA8 (15) | 2.128 vs 2.307 (−0.179) | 2.782 vs 2.927 (−0.145) | 4.079 vs 4.161 (−0.082) |
| 200K textured: RGB565 (22) vs RGBA8 (7) | 11.840 vs 11.860 (−0.020) | 9.567 vs 9.577 (−0.010) | 9.962 vs 9.984 (−0.022) |

**OBSERVATIONS**
- RGB565 was slightly faster in **every** paired comparison (6 fill pairs and 3 geometry pairs). In fill-bound tests the difference was 0.061–0.179 ms; in the geometry-bound test it was ≤ 0.022 ms, which is well within run-to-run noise.
- **The VRAM saving of RGB565 was not recorded in the CSV.** Both texture sets were created at startup, before any test was measured; VitaBench showed their VRAM cost on screen only. VRAM free values are therefore identical for every test within a build (§10).
- **Image quality (banding) was not assessed** in this benchmark.

---

## 6. Texture-memory allocation (MEASURED, test 25)
The probe allocated 512×512 RGBA8 textures (1 MB each) up to a safety cap of 48, while keeping at least 16 MB free.

| Build | Result note (verbatim) | VRAM free before | VRAM free after 48 MB | Change | RAM free before → after |
|---|---|---|---|---|---|
| MSAA off | `allocated 48 x 1024K RGBA8; stop=cap reached err=0x0; after delete RAM 64973K VRAM 84930K` | 84930 KB | 35777 KB | −49153 KB | 64973 → 64973 KB |
| MSAA 2x | `allocated 48 x 1024K RGBA8; stop=cap reached err=0x0; after delete RAM 64973K VRAM 82380K` | 82380 KB | 33227 KB | −49153 KB | 64973 → 64973 KB |
| MSAA 4x | `allocated 48 x 1024K RGBA8; stop=cap reached err=0x0; after delete RAM 64973K VRAM 77280K` | 77280 KB | 28127 KB | −49153 KB | 64973 → 64973 KB |

**OBSERVATIONS**
- All 48 MB were allocated **in VRAM** with **no GL error** in all three builds. vitaGL's RAM pool didn't change.
- Each 48 × 1024 KB allocation reduced free VRAM by exactly 49153 KB, i.e. 1 KB more than the texture data.
- After deletion, free VRAM returned to its starting value in every build.
- **The run stopped at the safety cap, not at a memory limit.** The real maximum texture memory was **not measured**; at least 28127–35777 KB of VRAM was still free.

---

## 7. Fill rate and transparency (MEASURED)

### Opaque full-screen textured layers
| Build | 1 layer | 2 layers | 4 layers | 8 layers | 8 − 1 | Per extra layer ((8−1)/7) |
|---|---|---|---|---|---|---|
| MSAA off | 1.972 | 2.008 | 2.066 | 2.307 | 0.335 ms | 0.0479 ms |
| MSAA 2x | 2.102 | 2.183 | 2.421 | 2.927 | 0.825 ms | 0.1179 ms |
| MSAA 4x | 2.707 | 2.926 | 3.335 | 4.161 | 1.454 ms | 0.2077 ms |

### Alpha-blended full-screen layers
| Build | Blend 4 vs opaque 4 | Blend 8 vs opaque 8 | Blend per extra layer ((8−4)/4) |
|---|---|---|---|
| MSAA off | 7.221 vs 2.066 (+5.155) | 13.289 vs 2.307 (+10.982) | 1.5170 ms |
| MSAA 2x | 8.161 vs 2.421 (+5.740) | 15.239 vs 2.927 (+12.312) | 1.7695 ms |
| MSAA 4x | 8.601 vs 3.335 (+5.266) | 15.386 vs 4.161 (+11.225) | 1.6962 ms |

**OBSERVATIONS**
- Opaque full-screen layers are cheap in these tests: 0.048–0.208 ms each. That's likely helped by the depth test (all layers are at the same depth with `GL_LEQUAL`) and by the GPU's tile-based design (an inference, not measured).
- **Blending is the most expensive operation measured:** each blended full-screen layer cost about **1.5–1.8 ms**, roughly 8–32× the cost of an extra opaque layer (1.5170 ÷ 0.0479 off, 1.7695 ÷ 0.1179 2x, 1.6962 ÷ 0.2077 4x). Eight blended layers took 13.289–15.386 ms, most of a 16.67 ms frame at 60 FPS.
- MSAA raises the cost of fill: 8 opaque layers rose from 2.307 (off) to 2.927 (2x) and 4.161 ms (4x).

---

## 8. Fog overhead (MEASURED)
| Build | Fill 4 layers: fog (23) vs no fog (14) | 200K textured: fog (24) vs no fog (7) |
|---|---|---|
| MSAA off | 3.893 vs 2.066 (+1.827) | 13.480 vs 11.860 (+1.620) |
| MSAA 2x | 3.947 vs 2.421 (+1.526) | 10.345 vs 9.577 (+0.768) |
| MSAA 4x | 4.269 vs 3.335 (+0.934) | 10.206 vs 9.984 (+0.222) |

**OBSERVATIONS**
- Fog added cost in **every** comparison, from +0.222 to +1.827 ms.
- In the fill test, fog cost much more than an extra opaque layer.
- **The size of the fog cost is inconsistent between builds**: for example, +1.620 ms (off) vs +0.222 ms (4x) on the same 200K scene. One run per build can't explain this (see §11).

---

## 9. Draw-call scaling: 50K triangles split into N draws (MEASURED)
| Build | 10 draws | 100 draws | 500 draws | 1000 draws | 1000 − 500 | Per extra draw (500→1000) | Per extra draw (10→1000) |
|---|---|---|---|---|---|---|---|
| MSAA off | 3.870 | 3.838 | 3.813 | 4.938 | 1.125 ms | 2.250 µs | 1.079 µs |
| MSAA 2x | 3.566 | 3.595 | 3.747 | 5.073 | 1.326 ms | 2.652 µs | 1.522 µs |
| MSAA 4x | 3.980 | 4.034 | 4.081 | 5.399 | 1.318 ms | 2.636 µs | 1.433 µs |

**OBSERVATIONS**
- From 10 to 500 draws the cost changed by at most 0.181 ms (MSAA 2x: 3.566 → 3.747). MSAA 4x changed by 0.101 ms (3.980 → 4.081); MSAA off was flat (3.870 → 3.813).
- Going from 500 to 1000 draws added **1.125–1.326 ms** in every build, about 2.3–2.7 µs per additional draw call.
- These draws only change the index range. Real draw calls also change textures, matrices or state and would cost more (not measured).

### Triangle throughput (for context; MEASURED, VBO, small 256×256 px area)
| Build | 10K − empty | 50K − empty | 100K − empty | 200K − empty | ns per triangle (200K) |
|---|---|---|---|---|---|
| MSAA off | 0.306 | 2.080 | 4.737 | 9.464 ms | 47.32 |
| MSAA 2x | 0.379 | 1.618 | 3.571 | 6.606 ms | 33.03 |
| MSAA 4x | 0.306 | 1.700 | 3.281 | 6.622 ms | 33.11 |

**Texture cost on geometry:** 50K textured − flat = +0.172 / +0.380 / +0.355 ms; 200K textured − flat = +0.618 / +1.040 / +1.055 ms (off / 2x / 4x).

---

## 10. Free RAM and VRAM (MEASURED)
| Build | RAM free (vitaGL pool), tests 1–24, before = after | VRAM free, tests 1–24, before = after | VRAM vs MSAA off |
|---|---|---|---|
| MSAA off | 64973 KB | 84930 KB | — |
| MSAA 2x | 64973 KB | 82380 KB | −2550 KB |
| MSAA 4x | 64973 KB | 77280 KB | −7650 KB |

**OBSERVATIONS**
- Free memory didn't change during any timed test: all resources were created before measurement, and no test allocates memory per frame.
- **MSAA's VRAM cost** (difference in free VRAM at startup): **2550 KB for 2x** and **7650 KB for 4x**.
- vitaGL's RAM pool was 64973 KB free in every build. This is **not** the Vita's total free system RAM, which wasn't measured.

---

## 11. Important findings and limitations

### Main findings (from the measurements above)
1. **VBOs are about 1.9–2.1× faster than client arrays** for static geometry in all builds (§4).
2. **Alpha blending is by far the most expensive operation measured:** about 1.5–1.8 ms per blended full-screen layer (§7).
3. **Draw calls are cheap up to 500 per frame** in this test; **1000** added about 1.1–1.3 ms (§9).
4. **RGB565 was marginally faster** than RGBA8 in every pairing, mainly in fill-bound tests (§5). Its memory saving and image quality are unmeasured.
5. **48 MB of extra textures fit in VRAM** with no error in all builds; the true limit wasn't reached (§6).
6. **MSAA cost is clear in fill-bound tests** (+0.130 to +1.951 ms) and **in VRAM** (2.5 MB for 2x, 7.5 MB for 4x) (§1, §10).
7. **Fog always costs something** (+0.222 to +1.827 ms), but the amount varies between builds (§8).

### Unexpected result: MSAA builds faster than MSAA off
In **16 comparisons**, an MSAA build had a **lower** average frame time than MSAA off. The largest cases:
- test 5, 200K flat: 2x 8.537, 4x 8.929 vs off 11.242 ms
- test 7, 200K textured: 2x 9.577, 4x 9.984 vs off 11.860 ms
- test 24, fog 200K textured: 2x 10.345, 4x 10.206 vs off 13.480 ms
- test 4, 100K flat: 2x 5.502, 4x 5.588 vs off 6.515 ms

MSAA does more work per pixel, so **these results are not plausible as a real effect and need repeated testing before anyone relies on them.** Possible contributors, none verified:
- **Single runs:** each build ran once, at a different time.
- **Device state differed between runs:** CPU/GPU clocks, temperature, battery level or charging, background tasks.
- **Run quality:** the MSAA-off run's heavy tests have about twice the frame-time spread of the MSAA runs (§2), which points to an unstable run.
- **Measurement method:** frame time is a CPU-side interval, and vitaGL/GXM queue work for the GPU asynchronously, so vertex-bound tests may partly measure CPU submission or pipeline effects.
- **Different binaries:** each build is a separate binary with different memory layouts (for example, VRAM free differs by up to 7650 KB).

Until repeated, the **only MSAA conclusions supported** by this data are the **fill-bound costs** and the **VRAM costs**. Geometry-bound comparisons between builds are **inconclusive**.

### Limitations of this benchmark
- **Not a game workload:** synthetic content only. **Results do not predict Mary Skelter Finale's performance or that of any real game.**
- **One run of 5 s per test per build;** no repeats, so no variance between runs.
- **CPU-side frame interval** only, with no GPU timing and no breakdown of CPU vs GPU time.
- **Vsync off.** Real play with vsync on is capped at the display refresh, and frame pacing differs.
- **The overlay is drawn in every frame** (included in the baseline).
- **Geometry tests** use about 1 px² triangles in a 256×256 px area: vertex-bound, not representative of real meshes.
- **Fill tests** use same-depth layers with the depth test on, which may make opaque overdraw look cheaper than in real scenes.
- **Not recorded:** texture-set VRAM costs, run timestamps, battery and temperature, and total system RAM.
- **The texture-memory probe** stopped at its 48 MB safety cap.

---

## 12. Provisional rendering recommendations (RECOMMENDATIONS, not measurements)

| # | Recommendation | Based on | Confidence |
|---|---|---|---|
| R1 | **Use VBOs for all static geometry.** Consider changing the main app's default from client arrays to VBO after one confirming run | §4: 1.87–2.07× in all builds | Higher |
| R2 | **Minimise alpha-blended screen area** (large transparent effects, full-screen fades, stacked UI panels). Treat each full-screen blended layer as roughly 1.5–1.8 ms | §7 | Higher |
| R3 | **Keep draw calls well below 1000 per frame**; batch geometry by material | §9 | Medium (state changes not tested) |
| R4 | **Prefer RGB565 for opaque textures** if image quality is acceptable on device | §5: small, consistent speed gain; memory saving unmeasured | Medium-low |
| R5 | **Budget for fog** as a measurable cost; use it where it reduces visible geometry | §8: always > 0, amount varies | Medium-low |
| R6 | **Keep MSAA off by default for now.** MSAA 2x is the candidate if fill-bound cost (+0.13 to +0.65 ms here) and 2.5 MB of VRAM are acceptable. 4x costs more in fill (up to +1.95 ms) and VRAM (7.5 MB) | §1, §7, §10; geometry results inconclusive | Low until repeated |
| R7 | **Plan texture budgets of tens of MB in VRAM.** At least 48 MB of extra textures fit with ≥ 28 MB remaining in every build; measure the real limit before relying on more | §6 | Medium |
| R8 | **Before changing defaults, repeat the benchmark:** each build at least 3 times, alternating order, device cool and on the charger, results kept per run | §11 | — |
| R9 | **Benchmark improvements for the next version** (needs a code change, not done): write texture-set VRAM costs and run index/time to the CSV; add a test with real draw-state changes; add a test with realistically sized triangles; optionally repeat each test automatically | §11 | — |

None of these recommendations has been applied. The working app's defaults (client arrays, RGBA8, MSAA off) are unchanged.
