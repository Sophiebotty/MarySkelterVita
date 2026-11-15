# Vita Port Compatibility Map: Mary Skelter Finale vs. the MarySkelterVita Project

**Date:** 2026-11-14
**Repository state at writing:** `6bf1ee0` (unchanged by this document)
**Type:** research and documentation only. Nothing was built, installed or modified.

### Sources
| Source | Kind of evidence |
|---|---|
| `TECHNICAL_AUDIT.md` | Static analysis of the PC executable (imports, strings) |
| `MARY_SKELTER_VITA_FEASIBILITY.md` | Static analysis, VitaSDK inspection, public sources |
| `STEAM_ARCHIVE_STRUCTURE.md` | Read-only structural analysis of the PC game files |
| `EXECUTABLE_RESOURCE_LOADING_AUDIT.md` | Static string and import analysis of the PC executable |
| `TECHNICAL_BASELINE.md`, `GRAPHICS_TEST_RESULTS.md` | Our Vita app: source code plus owner-reported **Vita hardware** tests |
| `BENCHMARK_RESULTS.md`, `VITA_PERFORMANCE_BUDGET.md` | **Vita hardware** measurements from VitaBench (synthetic content) |
| Project source tree (`src/`, `bench/`, `CMakeLists.txt`), re-inspected for this document | What is actually implemented |

### Labels
| Label | Meaning |
|---|---|
| **CONFIRMED** | Directly observed. The evidence type is always stated: **[HW]** Vita hardware test or measurement, **[SRC]** our source code, **[STATIC]** read-only analysis of Finale's PC files or executable, **[SDK]** installed VitaSDK inspection, **[PUB]** public source |
| **INFERRED** | A reasoned conclusion from confirmed evidence; not proven |
| **UNKNOWN** | Not established |

### Compatibility status values
| Status | Meaning |
|---|---|
| **Compatible** | Our Vita project already provides what Finale's system needs, and it has been tested on hardware |
| **Partially Compatible** | Some needed capability exists and is hardware-tested, but significant parts are missing or unverified |
| **Not Implemented** | Nothing in our project covers it yet. Feasibility may still be possible or unknown |
| **Unknown** | Too little is known about Finale's side, the Vita side, or both to judge |

### Ground rules for reading this map
- **Finale does not run on the Vita.** Nothing in this project loads, converts or executes any part of Mary Skelter Finale. No Finale code, data or assets are in this repository.
- **Our Vita project is not a game engine.** It's a small renderer and test application: one procedurally generated room, a first-person camera, grid collision and a debug overlay, plus a separate benchmark (VitaBench).
- **There is no original Vita version of Finale to compare against.** Finale shipped on PS4, Switch and PC; only the first series game (*Mary Skelter: Nightmares*) shipped on Vita [PUB]. Every Finale finding here describes **the PC release**. The PC formats aren't assumed to match the PS4 or Switch formats either. One asset path, `TEXTURE_NX_EN/…`, suggests a Switch lineage (INFERRED).
- **Hardware results vs analysis:** [HW] results come from the owner's modded PS Vita. Everything about Finale is [STATIC] analysis of PC files, not runtime behaviour.

---

## Summary matrix

| # | System | Finale (PC) uses | Our Vita project supports | Status |
|---|---|---|---|---|
| 1 | Resource archives and decompression | GDAT `.dat` + CSH `.csh` (zlib-consistent `BILZ` wrapper), PDA `.bra` | No file loading, no archive parsing, no zlib | **Not Implemented** |
| 2a | Textures | `GxResTexture{Bmp,Dds,Png,Pvr,Sdds,Tga}`, `tex`/`tid` resources | RGBA8 / RGB565 upload with mipmaps (procedural only) | **Partially Compatible** |
| 2b | 3D models and animation | `GxResModel`, `mdl`/`mdc`, motion packages, soft-body, IK | Static generated room mesh only | **Not Implemented** |
| 3a | Basic 3D rasterisation | Direct3D 11 | vitaGL fixed-function: depth, culling, VBO, fog, MSAA | **Partially Compatible** |
| 3b | Shaders and post-processing | Precompiled D3D shaders (`vsb`/`psb`/`spk`), YEBIS (D3D11), compute/tessellation classes | No custom shaders used; no compute or tessellation available | **Not Implemented** |
| 4 | Audio and voice | Ogg in BRA, CRI `.acb`, lip data, XAudio2/DirectSound, CRI-named layer | No audio at all | **Not Implemented** |
| 5 | Video | 26 × H.264/AAC MP4, 1920×1088 | No video playback | **Not Implemented** |
| 6 | Input and controls | DirectInput, XInput, scePad, `GxPad*`, `GxTouch` | SDL GameController, deadzone, hot-plug (hardware-tested) | **Partially Compatible** |
| 7 | Memory management | Unknown | Partial measurements from VitaBench; provisional budget | **Unknown** |
| 8 | Save data | `PC_SAVE.dat`-type strings; Win32 file APIs | No save system | **Not Implemented** |

---

## 1. Resource archives and decompression

### 1.1 What Finale appears to use
| Finding | Label |
|---|---|
| 37 `GxArchivedFile###.dat` (051–088, **087 missing**): `GDAT` + u32 count + (offset, size) index; entries ordered, non-overlapping and gap-free (18,958 pairs checked) | CONFIRMED [STATIC] |
| 37 `GxArchivedTable###.csh`: 128-byte header, `BILZ` block, stream starting `78 DA`; uncompressed size = 12 + 28 × the matching DAT's entry count in all 37 | CONFIRMED [STATIC] |
| DAT entries carry the same 128-byte header + `BILZ` + `78 DA` wrapper (1,569 sampled) | CONFIRMED [STATIC] |
| The wrapper payloads are zlib-compressed | INFERRED: `78 DA` is consistent with zlib; nothing was decompressed or validated |
| The executable embeds zlib 1.2.3 and has a `GxZlibReadStream` class | CONFIRMED [STATIC] (string presence) |
| Each `.csh` holds 28-byte records describing its DAT's entries (IDs or hashes) | INFERRED |
| Loading is done by the Gx core resource system (`GxSysResourceArchive`, `GxSysResource`, async loader threads) | INFERRED (strong); the code section was not analysable |
| `.bra` archives: `PDA\0` v2, trailing named directory, gap-free data; about 32,700 Ogg entries, about 32,500 compressed extensionless entries, 429 `.acb` | CONFIRMED [STATIC] |
| `.bra` replaces development-time CRI `.cpk` packs (SOUND, SOUND_EN, VOICE_IL) | INFERRED (path strings) |
| Meaning of the CSH record fields, how lookups work, whether `GDAT`/`BILZ`/`PDA` are validated at runtime | **UNKNOWN** |

### 1.2 What our Vita project supports
| Capability | Label |
|---|---|
| The main app reads **no files at all**: geometry and textures are generated in code | CONFIRMED [SRC] (no `fopen`/`sceIo`/`SDL_RWFrom` in `src/`) |
| Writing a file to `ux0:data/…` with standard C I/O works (VitaBench CSV output) | CONFIRMED [HW] |
| zlib is **not installed** in our VitaSDK | CONFIRMED [SDK] |
| Archive parsing (GDAT, CSH, PDA) | Not present [SRC] |

### 1.3 Status: **Not Implemented**
The containers are partly understood, but nothing on the Vita side exists. Reading the outer containers is INFERRED to be straightforward (fixed little-endian headers, zlib, a simple directory). That alone doesn't give usable assets, because the entry formats are unknown (§2–§4).

### 1.4 Work remaining
1. A Vita file-I/O layer (`sceIo` or SDL RWops) with buffered and asynchronous reads.
2. A zlib inflater for ARMv7, from portable source code that needs to be added to the toolchain.
3. GDAT, CSH and PDA readers. Test them only against **synthetic archives we create ourselves**, never Finale's files, until permission exists.
4. Measure decompression throughput and memory use on the Vita. The `BILZ` header ratios (median 5.26, max 506 in the sample) suggest assets expand a lot in memory (INFERRED).

### 1.5 Permissions and documentation needed
- **Rights-holder permission** to read or convert Finale's archives on any device for this project.
- **Proprietary documentation:** the CSH record layout and lookup scheme, and the `GDAT`/`BILZ`/`PDA` specifications.
- **No SDK access is needed** for the container code itself; zlib is open source.

---

## 2. 3D models and textures

### 2.1 What Finale appears to use
| Finding | Label |
|---|---|
| Texture classes `GxResTextureBmp`, `Dds`, `Png`, `Pvr`, `Sdds`, `Tga`; strings `png`, `pvr`, `tga` next to them | CONFIRMED [STATIC] (string presence) |
| `tex` tag next to `GxResTexture`; `.tid` and `.tex` file paths such as `SYSTEM/Keyboard.tid` | CONFIRMED [STATIC] |
| Model and animation classes: `GxResModel`, `GxResModelConfig(Ext)`, `GxMeshFile`, `GxMaterialFile`, `GxResMotionPackage(Gmp)`, `GxResMotionIK`, `GxResSoftbodySimulation`; tags `mdl`, `mdc`, `mpk`, `gmp` | CONFIRMED [STATIC] (string presence) |
| Which image format is inside PC `tex` resources (DDS? PVR? other?) | **UNKNOWN** |
| Model, skinning, animation and soft-body data formats | **UNKNOWN** |
| Whether PC assets match the PS4/Switch assets | **UNKNOWN** (`TEXTURE_NX_EN` suggests Switch lineage, INFERRED) |

### 2.2 What our Vita project supports
| Capability | Label |
|---|---|
| RGBA8 and RGB565 texture upload, mipmaps (`glGenerateMipmap`), trilinear and nearest filtering | CONFIRMED [HW] (rendered correctly; 565 switching observed) |
| Textures are **procedurally generated**; there is no image file loader (PNG, DDS, TGA or PVR) | CONFIRMED [SRC] |
| No image decoder library is installed in the VitaSDK | CONFIRMED [SDK] |
| Compressed texture formats: vitaGL defines DXT, PVRTC and ETC1 constants | CONFIRMED [SDK] header; **never used or tested** [SRC] |
| Geometry: static indexed mesh built from a grid map; 24-byte vertices; VBO and client-array paths | CONFIRMED [HW] |
| Model loading, skinned meshes, skeletal animation, morph targets, soft-body | Not present [SRC] |
| Geometry budget measured (synthetic): 200K triangles 9.577–11.860 ms; 50K textured 3.929–4.362 ms | CONFIRMED [HW] (VitaBench; tiny triangles) |
| 48 MB of extra textures allocated in VRAM without error | CONFIRMED [HW] |

### 2.3 Status
- **Textures: Partially Compatible.** Basic upload and sampling work on the Vita, but Finale's actual texture formats are unknown and no loaders exist.
- **Models and animation: Not Implemented.**

### 2.4 Work remaining
- Identify the format inside `tex` resources; needs documentation.
- Texture loaders and/or an offline converter to Vita-friendly formats (RGB565, PVRTC or DXT, after VitaBench tests compressed formats).
- Downscale textures to fit VRAM (§7).
- A model loader, skinning (CPU or vertex-shader), an animation system, and decisions on soft-body/IK (likely simplified or removed, INFERRED). All depend on format documentation.
- Measure realistic meshes: VitaBench used about 1 px² triangles (`VITA_PERFORMANCE_BUDGET.md` §8, V8).

### 2.5 Permissions and documentation needed
- **Rights-holder documentation or source** for the `tex`, `mdl`, `mdc`, `mpk` and `gmp` formats, **or** original source assets.
- **Permission** to convert and distribute converted assets.
- **No SDK beyond VitaSDK** is needed for loaders themselves.

---

## 3. Rendering and shaders

### 3.1 What Finale appears to use
| Finding | Label |
|---|---|
| Direct3D 11 main renderer (`D3D11CreateDeviceAndSwapChain`); D3D9Ex/DXVA2 for video presentation | CONFIRMED [STATIC] imports / video role INFERRED |
| `GxRenderHullShader`, `GxRenderDomainShader` (tessellation stages) and `GxSysGpgpu` (compute) classes | CONFIRMED [STATIC] (string presence); actual use UNKNOWN |
| Shader resources `vsb` → `GxResShaderVertex`, `psb` → `GxResShaderPixel`, `spk` → `GxResShaderPackage`; `D3DReflect` imported | CONFIRMED [STATIC] (adjacency, import) |
| Shaders ship precompiled for Direct3D | INFERRED (strong) |
| YEBIS post-processing with a D3D11 back end; `…__ps_5_0` in YEBIS shader names | CONFIRMED [STATIC] |
| Shader model of the game's own shaders | **UNKNOWN** |
| OROCHI / Mizuchi renderer | INFERRED (OROCHI strings present; not officially confirmed) |

### 3.2 What vitaGL and our project provide
| Capability | Label |
|---|---|
| vitaGL implements OpenGL ES 2.0 plus fixed-function emulation over GXM | CONFIRMED [SDK] |
| No `glDispatchCompute` or `glPatchParameteri` symbols in vitaGL, so **no compute or tessellation** | CONFIRMED [SDK] |
| Runtime GLSL/Cg compilation (`glShaderSource`, `glCompileShader`) and `glShaderBinary` exist in vitaGL | CONFIRMED [SDK]; **never used or tested by our project** [SRC] |
| Immediate mode is unusable under SDL2 (legacy pool 0); vertex arrays and VBOs required | CONFIRMED [HW] (crash and fix) |
| Our renderer: perspective camera, depth test, back-face culling, textured indexed geometry, fog, VBO and client paths, MSAA 2x/4x builds, shader warm-up, vsync | CONFIRMED [HW] |
| Measured costs: VBO about 1.9–2.1× faster than client arrays; each blended full-screen layer 1.517–1.770 ms; draw calls flat to 500, +1.125–1.326 ms at 1000 | CONFIRMED [HW] (VitaBench; CPU-side frame-start intervals, one run per build) |
| MSAA vs off for geometry-heavy tests | **Inconclusive**: MSAA builds measured faster in 16 comparisons; needs repeating |
| GPU time separate from CPU | **UNKNOWN** (not measured) |

### 3.3 Status
- **Basic 3D rasterisation: Partially Compatible.** The Vita stack draws textured, depth-tested, fogged geometry at measured costs. Finale's material, lighting and post-processing model is unknown.
- **Shaders and post-processing: Not Implemented.** No custom shaders exist in our project, Finale's D3D shaders can't run on vitaGL, and compute and tessellation aren't available.

### 3.4 Work remaining
1. A custom-shader path in our renderer: compile and test Cg/GLSL shaders on hardware, and measure their cost.
2. Recreate Finale's shaders and materials from source, or design equivalents. Requires knowing the material model.
3. Replace or remove YEBIS effects (depth of field, glare, motion blur) with cheap equivalents, within the blended-pixel budget.
4. Remove or redesign any tessellation and compute use, once it's known whether they're used.
5. Validation from the performance budget: V1–V3, plus state-change cost (V7) and realistic triangles (V8).

### 3.5 Permissions and documentation needed
- **Shader source code** or a material specification (rights holder).
- **YEBIS:** Silicon Studio licence and whether any Vita path exists; none was found [PUB].
- **OROCHI:** Silicon Studio licence for any engine reuse.
- **Official Sony Vita SDK:** only if licensed middleware built for it were used; compatibility with the homebrew toolchain is UNKNOWN.

---

## 4. Audio and voice

### 4.1 What Finale appears to use
| Finding | Label |
|---|---|
| Imports XAudio2_9 and DirectSound | CONFIRMED [STATIC] |
| CRI-named classes: `GxSysCriAdx2`, `GxUnitCriAtomStream`, `GxResCriAtomCueSheetBinary` (`acb`), `GxResCriAtomConfiguration` (`acf`) | CONFIRMED [STATIC] (string presence) |
| `.bra` archives: Ogg containers (`OggS` at +16) stored without additional BRA-level compression, in `BGM`, `BGM_F`, `JINGLE`, `SE`, `VOICE`, `VOICE_EN`, `VOICE_IL` | CONFIRMED [STATIC] |
| Ogg Vorbis decoder strings (`vorbis_synthesis_headerin failed`, `ogg_stream_init failed`, …) in the executable | CONFIRMED [STATIC] |
| The Ogg streams contain Vorbis | INFERRED (decoder strings); codec not checked in the files |
| 429 `.acb` entries (CRI cue sheets) | CONFIRMED by extension [STATIC]; format INFERRED |
| About 32,500 compressed extensionless records (for example `voice\event\lip\…`), likely lip-sync data | CONFIRMED [STATIC] (names, sizes) / purpose INFERRED / format UNKNOWN |
| An extension list `.hca .ogg .usm .mp4 .mkv` and non-Gx `pr…`/`mm…` audio strings | CONFIRMED [STATIC]; role INFERRED (a PC substitution layer) |
| Loop points, cue logic, 3D audio parameters | **UNKNOWN** |

### 4.2 What the Vita and our project provide
| Capability | Label |
|---|---|
| **Our app has no audio**: SDL is initialised without `SDL_INIT_AUDIO`; no audio code exists | CONFIRMED [SRC] |
| VitaSDK has `SceAudio` (output) and `SceAudiodec` stubs and headers (decoder types AT9, AAC, MP3, CELP) and a `SceNgs` stub (no header) | CONFIRMED [SDK] |
| No Ogg/Vorbis/Opus decoder library installed | CONFIRMED [SDK] |
| SDL2's audio subsystem on Vita | Present in the library; **untested** in our project |

### 4.3 Status: **Not Implemented**

### 4.4 Work remaining
1. An audio output layer (SDL audio or `SceAudio`), tested on hardware with original test sounds.
2. A Vorbis decoder (portable open-source libraries exist; not installed). Measure its CPU cost on the Vita, which is UNKNOWN.
3. Streaming BGM from storage, plus voice playback (tens of thousands of short clips).
4. `.acb` cue-sheet handling: a CRI ADX2 runtime licence or format documentation, or replacement data.
5. The lip-sync data format and its consumer; needs documentation.

### 4.5 Permissions and documentation needed
- **CRI Middleware** licence for any ADX2/ACB runtime on the Vita.
- **Rights-holder documentation** for `.acb` use, lip data and BRA field semantics.
- **Permission** to use the audio content at all.

---

## 5. Video playback

### 5.1 What Finale appears to use
| Finding | Label |
|---|---|
| 26 MP4 files (22 in `MOVIE_EN`, 4 in `MOVIE_LOGO`), totalling 2,211,176,715 + 6,666,703 bytes | CONFIRMED [STATIC] |
| H.264 (`avc1`) at 1920×1088 (Ghostlight logo 1920×1080) with AAC audio (except 500004, video only); durations 4.0–469.0 s | CONFIRMED [STATIC] (MP4 metadata) |
| Path templates for `MOVIE_JP` / `KR` / `CN` / `EN`; only EN and LOGO are installed | CONFIRMED [STATIC] |
| Playback through Media Foundation behind Sofdec-named Gx classes | INFERRED (imports + strings) |

### 5.2 What the Vita and our project provide
| Capability | Label |
|---|---|
| `SceAvPlayer` and `SceVideodec` stubs and headers in the VitaSDK | CONFIRMED [SDK] |
| Video playback in our project | Not present [SRC] |
| The Vita's decoder limits (maximum resolution, profile, bitrate) | **UNKNOWN**: not tested |

### 5.3 Status: **Not Implemented** (Vita feasibility **Unknown** until tested)

### 5.4 Work remaining
1. A playback prototype with **original test videos** at several resolutions and bitrates, to measure decoder limits on hardware.
2. Decide on re-encoding (resolution, bitrate, audio codec) to fit the decoder and storage. INFERRED: 1080p sources will need re-encoding.
3. Integrate video output with the GL renderer (texture upload or overlay) and measure its cost.

### 5.5 Permissions and documentation needed
- **Rights-holder permission** to re-encode and redistribute the videos.
- **No proprietary SDK** expected for `SceAvPlayer` through VitaSDK, but its homebrew usability is **untested**.

---

## 6. Input and controls

### 6.1 What Finale appears to use
| Finding | Label |
|---|---|
| DirectInput 8, XInput 1.4, `libScePad.dll` (loaded at runtime) | CONFIRMED [STATIC] |
| `GxPad`, `GxPadPc`, `GxPadPs4`, `GxPadXbox`, `GxTouch`, `GxVibration` classes; keyboard bindings in `MarySkelterFinale.ini` | CONFIRMED [STATIC] |
| Finale's action map (which buttons do what in dungeon, battle and menus) | **UNKNOWN** (not inspected; would come from the game or its manual) |

### 6.2 What our Vita project provides
| Capability | Label |
|---|---|
| SDL GameController wrapper: sticks with a circular deadzone (0.25) and response curve, D-pad, button press edges, hot-plug, touch-to-mouse disabled | CONFIRMED [SRC]; sticks, D-pad movement and START confirmed [HW] |
| Front and rear touchpad input in our app | Not used [SRC]; `SceTouch` present [SDK] |
| Motion sensors | Not used [SRC]; `SceMotion` present [SDK] |
| Vibration | Not available on Vita hardware (INFERRED, public knowledge; not verified here) |

### 6.3 Status: **Partially Compatible**
The basic input path is hardware-tested. Finale's actions and UI flow are unknown, and touch isn't implemented.

### 6.4 Work remaining
- A control map for Vita buttons (no L2/R2/L3/R3 on Vita, INFERRED), with optional touch for menus and the map.
- UI legibility at 960×544 (UI re-layout depends on assets).
- Remove the vibration paths.

### 6.5 Permissions and documentation needed
Little technical dependency. Re-using Finale's UI, icons or text needs **rights-holder permission**.

---

## 7. Memory management

### 7.1 What Finale appears to use
| Finding | Label |
|---|---|
| Finale's runtime memory use on any platform | **UNKNOWN** (not measured; PC build not executed) |
| Install size 9.08–9.15 GB; `BILZ` sampled expansion ratios median 5.26 | CONFIRMED [STATIC]; in-memory footprint UNKNOWN |
| Switch hardware (target lineage) has about 4 GB RAM | INFERRED (public spec) |

### 7.2 What VitaBench measured (actual results)
| Measurement | Value | Label |
|---|---|---|
| vitaGL VRAM pool free at startup | 84930 KB (MSAA off), 82380 KB (2x), 77280 KB (4x) | CONFIRMED [HW] |
| MSAA VRAM cost | 2550 KB (2x), 7650 KB (4x) | CONFIRMED [HW] |
| vitaGL RAM pool free (`VGL_MEM_RAM`) | 64973 KB in every build | CONFIRMED [HW] |
| Extra textures allocated | 48 × 1 MB RGBA8 with no error; stopped at the probe's safety cap, not a limit; 28127–35777 KB still free | CONFIRMED [HW] |
| Total free system RAM, heap use, CPU memory budget | — | **UNKNOWN** |
| Maximum texture memory | — | **UNKNOWN** |

### 7.3 Provisional budget (from `VITA_PERFORMANCE_BUDGET.md`; targets, not limits)
- ≤ 48 MB total resident textures (a planning value, not a measured limit).
- ≥ 16 MB of VRAM kept free (a provisional margin).
- MSAA reserve of 2.5 MB (2x) or 7.5 MB (4x) if enabled.
- The CPU-side RAM budget is still to be set after measurement.

### 7.4 Status: **Unknown**
Our GPU memory headroom is partly measured; Finale's needs and the Vita's total system memory are not.

### 7.5 Work remaining
1. Measure total free system RAM and heap behaviour on the Vita (V10).
2. Raise the texture probe cap carefully to find the real VRAM limit.
3. Estimate per-scene memory for a representative Finale dungeon. This needs rights-holder data, or reference figures from the Switch build.
4. Design streaming and caching, informed by the Gx async-loader design (if documented).

### 7.6 Permissions and documentation needed
**Switch or PS4 memory budgets and asset sizes** from the rights holder would remove most guesswork.

---

## 8. Save data

### 8.1 What Finale's executable audit establishes
| Finding | Label |
|---|---|
| Strings `PC_SAVE.dat`, `SaveData`, `System Data`, `SystemData` in `.rdata`, plus data file names such as `modelset.dat`, `dunInfo.dat`, `JobStatus.dat` | CONFIRMED [STATIC] (string presence only) |
| File-system imports usable for saves: `SHGetFolderPathW`, `CreateDirectoryA/W`, `CreateFileA/W`, `WriteFile`, `FlushFileBuffers`, `SetEndOfFile`; INI functions (`GetPrivateProfileStringA`, `WritePrivateProfileStringA`) | CONFIRMED [STATIC] (imports; actual save path usage UNKNOWN) |
| Where the PC version stores saves (folder), the save format, its size, integrity checks, encryption, Steam Cloud use | **UNKNOWN**: not inspected; the audits didn't establish any of this |
| Which strings are save files vs. read-only data | **UNKNOWN** |

### 8.2 What the Vita and our project provide
| Capability | Label |
|---|---|
| A save system in our app | Not present [SRC] |
| Writing files to `ux0:data/…` with standard C I/O | CONFIRMED [HW] (VitaBench CSV) |
| `SceAppUtil` stub and header (the Vita's system save-data utilities) | CONFIRMED [SDK]; specific save functions not inspected or tested |

### 8.3 Status: **Not Implemented**

### 8.4 Work remaining
- Design a Vita save scheme: location, atomic writes, corruption handling, suspend/resume safety.
- Decide between `SceAppUtil` save data and plain files; test both on hardware.
- Finale save compatibility (importing PC saves) would need the save format, which is **UNKNOWN**. It isn't a goal unless the rights holder asks for it.

### 8.5 Permissions and documentation needed
- **The save format** from the rights holder, if compatibility or structure reuse is wanted.
- **No proprietary SDK** expected for basic homebrew saves.

---

## 9. Biggest technical blockers
1. **No source code or engine access.** Finale's loading, game logic and rendering live in the Gx framework (with OROCHI strongly indicated) inside a code section we can't analyse. Without source, nothing of Finale's runtime can be ported, only replaced.
2. **Unknown asset formats.** The CSH records, every `GxRes*` payload (textures, models, motions, shaders, `ResEnc`/`ResDat`/`ResBin`), the lip data and `.acb` usage are all undocumented.
3. **Direct3D shaders and post-processing.** Precompiled D3D shaders and YEBIS (D3D11) can't run on vitaGL. A rewrite requires source and a material specification; compute and tessellation are unavailable.
4. **Middleware licensing.** CRI (ADX2/ACB, Sofdec heritage), YEBIS and OROCHI each need their owners' agreement for any Vita use.
5. **Hardware fit.** Finale's content targets PS4/Switch-class memory. Our measured Vita GPU memory headroom is tens of MB, and total system RAM is unmeasured.
6. **Legal and distribution.** Using Finale's data needs rights-holder permission. The PSN Vita store has accepted no new titles since 2021-07-20 [PUB].

---

## 10. Most important unanswered questions for Idea Factory
Extends `MARY_SKELTER_VITA_FEASIBILITY.md` §12 with what Phase 7 found.

1. **Permission:** may this project read, convert or prototype Finale's data on a Vita, and under what written terms?
2. **Engine:** is Finale built on Silicon Studio OROCHI (which version)? Is `Gx` OROCHI's framework or Compile Heart's? Can engine source or an OROCHI Vita back-end be accessed?
3. **Archives:** what are the CSH 28-byte record fields and the lookup scheme, and the `GDAT`/`BILZ`/`PDA` specifications? Why is archive 087 absent? Are archives 062–086 language or region variants?
4. **Asset formats:** documentation for `tex`, `tid`, `mdl`, `mdc`, `mpk`, `gmp`, `vsb`, `psb`, `spk`, `CL3`, `enc`, `dat`, `bin`, `ffu` and `olf` resources.
5. **Rendering:** is Mizuchi used? Shader source and the material model? Which YEBIS effects are essential?
6. **Audio:** are the PC Ogg files derived from CRI HCA, and the BRA archives from CPK? How are `.acb` cue sheets used on PC? What is the lip-sync data format? What does "IL" mean?
7. **Saves:** the save format and where it's stored.
8. **Budgets:** memory budgets and asset sizes for the Switch build, as the closest reference.
9. **Middleware:** can CRI, YEBIS and OROCHI licences cover a Vita adaptation?
10. **Distribution:** is any form of Vita build acceptable (for example private test builds only)?

---

## 11. Recommended order of future development milestones

**Stage A: no rights-holder material needed** (original content only; each step tested on hardware)

| # | Milestone | Answers |
|---|---|---|
| A1 | Repeat VitaBench runs (V1–V3) and decide the draw-path and MSAA defaults | Inconclusive MSAA data; VBO default |
| A2 | VitaBench v2: state-change cost, realistic triangle sizes, compressed textures, vsync-on pacing, system RAM and the real VRAM limit (V4–V11) | Unknowns in §2, §3, §7 |
| A3 | Custom-shader prototype (Cg/GLSL via vitaGL), measured on hardware | Whether shader recreation is viable (§3) |
| A4 | Audio prototype: output, plus an Ogg Vorbis decoder (open-source library; installing it needs approval) using original test audio; measure CPU cost | §4 |
| A5 | Video prototype: `SceAvPlayer` with original test MP4s at several resolutions; measure decoder limits | §5 |
| A6 | File I/O and save prototype: async reads, a zlib inflater, and a Vita save scheme, tested with synthetic data and archives we create ourselves | §1, §8 |
| A7 | Asset pipeline prototype for original content: a model format, a simple skinned animation, texture conversion | §2 |

**Stage B: gated on Idea Factory's answers and permission**

| # | Milestone | Requires |
|---|---|---|
| B1 | Format specifications reviewed; feasibility re-assessed against Stage A measurements | Documentation (§10, items 3–8) |
| B2 | One converted representative scene (dungeon section and party) measured against the budget | Permission and formats or source assets |
| B3 | Engine and middleware path decided (port the Gx/OROCHI code vs reimplement) | Source and licences |
| B4 | Vertical slice | All of the above |

**Not recommended:** reverse-engineering the encrypted or packed code section, decompressing or converting Finale's archives, or any DRM work. These are outside the agreed scope and the legal position in `TECHNICAL_AUDIT.md` §8.

---

## 12. Bottom line
- **What exists:** our project has a **hardware-tested basic 3D rendering, input and collision foundation**, plus measured cost data for basic rendering operations on the Vita. It is not a game engine, and it supports none of Finale's formats.
- **What's understood about Finale:** the **container level** of its PC files (archives, sound packs, MP4s) and **where** in the executable loading appears to happen.
- **What's missing:** almost everything needed to use Finale's content (asset formats, shaders, engine logic, middleware rights) depends on the rights holder.
- **Achievable now:** Stage A work makes the Vita side ready and measured.
- **Not supported by current evidence:** any claim that Finale can run on the Vita.
