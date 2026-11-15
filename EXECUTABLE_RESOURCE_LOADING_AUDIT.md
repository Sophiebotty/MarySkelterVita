# Mary Skelter Finale: Executable Resource-Loading Audit (Static, Read-Only)

**Phase:** 7, static executable investigation  
**Date:** 2026-11-14  
**Executable:** `C:\Program Files (x86)\Steam\steamapps\common\Mary Skelter Finale\MarySkelterFinale.exe`, 11,393,304 bytes, SHA-256 `5C16ABFD…`, the same file as `TECHNICAL_AUDIT.md`  
**Related reports:** `TECHNICAL_AUDIT.md`, `STEAM_ARCHIVE_STRUCTURE.md`  
**Vita project state:** `314bd81`, no tracked file modified by this work

### Method and limits
- **How the file was read:** opened once, read-only (`open(..., "rb")`). The scripts parsed the PE headers and full import table, searched readable bytes for ASCII and UTF-16LE strings, and listed short printable strings around matches.
- **What was not done:** the executable was not run, patched, unpacked, decrypted, debugged or disassembled, and the DRM wrapper was not analysed. No game archive was decompressed or decoded.
- **Key limitation:** The `.text` section has very high entropy (8.00 bits per byte). This is consistent with encryption or packing, but entropy alone does not confirm either. The section's encryption or packing status remains unverified.
  - Static evidence therefore comes **only from readable data sections** (`.rdata`, `.data`) and the import table.
  - Constants embedded in machine code can't be seen. Which functions use which strings is **not observable** without analysing the code section further, which is outside the permitted scope.

### Labels
| Label | Meaning |
|---|---|
| **CONFIRMED** | Directly observed bytes or structures in the file (strings, imports, section properties) |
| **STRONG HYPOTHESIS** | A conclusion supported by several independent pieces of static evidence, not proven |
| **HYPOTHESIS** | Plausible, with limited support |
| **UNKNOWN** | Not determinable statically under the read-only rules |

> **Reminder:** a string being present in the executable does **not** prove that the code path using it runs, or even that the string is used. Each conclusion below is marked accordingly.

---

## 1. References to `GxArchivedFile` and `GxArchivedTable`

**CONFIRMED:**
- Each name appears **once**, in `.rdata`, at file offsets `0x728160` and `0x728170`. There are no UTF-16 occurrences.
- **Neighbouring strings (in order):** `GxResTextureSdds`, `png`, `GxResTexturePng`, `pvr`, `GxResTexturePvr`, `tga`, `GxResTextureTga`, **`GxArchivedFile`**, **`GxArchivedTable`**, `original`, `finalizedCommon`.
- **A second cluster** at about `0x7285a0`–`0x7286f0`:

  `GxSysResourceAsyncLoadMultiWithSharedQueue::GxAsyncLoadThreadMultiOpt` · **`GxSysResourceArchive`** · **`.csh`** · **`%03d`** · `GxSysResource` · `_originalRootPath` · `_finalizedCommonRootPath` · `_finalizedNativeRootPath` · `_masterRootPath` · `createResourceRoutine failed:0` · `loadResourceSync failed:1` · `loadResourceSync failed:2` · `createResourceRoutine failed:1` · `%s%c%s.%s` · `file open Error:0` · `file open Error:`

- **No other format string matched:** no `%03d.dat`, `%03d.csh` or `GxArchivedFile%03d` was found. The pieces exist separately (`GxArchivedFile`, `GxArchivedTable`, `.csh`, `%03d`).
- **The literal `.dat`** occurs in `.rdata` 31 times, mostly in save and data file names such as `PC_SAVE.dat`, `modelset.dat` and `dunInfo.dat`, and as a type tag (§2).

**STRONG HYPOTHESIS:** the Gx resource-archive subsystem (`GxSysResourceArchive`) builds the archive names at runtime from a base name, a three-digit number (`%03d`) and an extension. That would match the shipped names `GxArchivedFile051.dat` … and `GxArchivedTable051.csh` …. How names are composed, and which numbers are requested, is **UNKNOWN**.

**HYPOTHESIS:** the root-path fields (`_masterRootPath`, `_originalRootPath`, `_finalizedCommonRootPath`, `_finalizedNativeRootPath`) and the strings `original` and `finalizedCommon` suggest the resource system can resolve files from **several roots**: packed archives versus loose "original" or "finalized" files. Which roots the shipped build uses is **UNKNOWN**. Only `masterWindows` exists on disk.

---

## 2. Format markers and resource type tags

### 2.1 `GDAT`, `BILZ`, `PDA` (CONFIRMED absent as literals)
None of `GDAT`, `BILZ` or `PDA`/`PDA\0` occurs as an ASCII or UTF-16 literal anywhere in the file.

**HYPOTHESIS:** the readers compare these magic values as integer constants inside the code section (high entropy; not inspected), or don't check them at all. Which of the two is **UNKNOWN**.

### 2.2 Type tags (CONFIRMED string adjacency)
The reversed byte forms stored in the archives (for example `\0xet`) were **not** found in readable sections. The only hit, `\0flo`, is part of the word "float" and is a false positive.

The **forward** three-letter codes, however, appear as short strings **immediately before** resource class names. That is the layout of a type-registration table:

| Tag in archives (§4.2 of `STEAM_ARCHIVE_STRUCTURE.md`) | String found | Adjacent identifier |
|---|---|---|
| `tex` | `tex` | `GxResTexture` (then `_textureInformation`) |
| `mdl` | `mdl` | `GxResModel`, `GxResModel::GxHeader` |
| `mdc` | `mdc` | `GxResModelConfigExt` |
| `spk` | `spk` | `GxResShaderPackage` |
| `vsb` | `vsb` | `GxResShaderVertex` (after `GxResShaderBase`) |
| `psb` | `psb` | `GxResShaderPixel` |
| `mpk` | `mpk` | `GxResMotionPackage` |
| — | `gmp` | `GxResMotionPackageGmp` |
| `csh` | `.csh` | `GxSysResourceArchive` cluster (§1) |
| `enc` | `enc` | `ResEnc`; also paths such as `TTM/data/event.enc` |
| `dat` | `dat` | `ResDat` |
| `bin` | `bin` | `ResBin`; also paths such as `TTM/Text/TextDataEn.bin` |
| `tid` | `.tid` | file paths such as `SYSTEM/Keyboard.tid`, `SYSTEM/WINDOW/icon01.tid` |
| `ffu` | `.ffu` | font paths such as `SYSTEM/WINDOW/FONT/sysfont_en.ffu` (also `_jp`, `_cn`) |
| `olf` | `.olf` | `license.olf` (near `GxShader`) |
| `CL3` | `CL3` | near `FILE_LINK`, `COLLECTION` |
| — | `acb`, `acf` | `GxResCriAtomCueSheetBinary`, `GxResCriAtomConfiguration` |

Texture-format strings next to texture classes: `png`, `pvr`, `tga` beside `GxResTexturePng`, `GxResTexturePvr` and `GxResTextureTga`. `GxResTextureBmp`, `GxResTextureDds` and `GxResTextureSdds` also exist.

**STRONG HYPOTHESIS:**
- The archive entry tags are **resource-type identifiers** that select a resource class: `tex` → texture, `mdl` → model, `vsb`/`psb` → vertex/pixel shader binaries, `spk` → shader package, `mpk`/`gmp` → motion packages, `mdc` → model configuration.
- Tags whose classes lack the `Gx` prefix (`enc` → `ResEnc`, `dat` → `ResDat`, `bin` → `ResBin`) are likely **game-specific** resource types registered with the same system.

**UNKNOWN:** the internal formats behind these classes. `enc` could mean "encoded" or "encrypted"; that remains unverified.

---

## 3. Resource-management systems (strings, RTTI, classes)

### 3.1 CONFIRMED identifiers
- **181 unique `Gx` identifiers** relate to resources, files, streams, loading or asset types. Relevant ones:
  - **Resource system:** `GxSysResource`, `GxSysResourceArchive`, `GxSysResourceRegister`, `GxSysResourceCallback`, `GxSysResourceAsyncLoad`, `GxSysResourceAsyncLoadBase`, `GxSysResourceAsyncLoadMulti`, `GxSysResourceAsyncLoadMultiWithSharedQueue`, `GxAsyncLoadThread`, `GxAsyncLoadThreadMulti`, `GxAsyncLoadThreadMultiOpt`, `GxResBase`, `GxResHeader`
  - **File and stream I/O:** `GxSysFile`, `GxFile`, `GxFileInformation`, `GxFilePath`, `GxFileStream`, `GxStream`, `GxStreamReader`, `GxMemoryStream`, `GxRingStream`, **`GxZlibReadStream`**
  - **Asset classes:** `GxResTexture*` (Bmp, Dds, Png, Pvr, Sdds, Tga), `GxResModel*`, `GxResShader*` (Base, Vertex, Pixel, VO, PO, GO, EO, CO, Package), `GxResMotion*`, `GxResSound*`, `GxResCriAtom*`, `GxResCollision*`, `GxResSoftbody*`, `GxResHudSet`, `GxResFlowChart`, and others
- **RTTI with namespaces:**
  - `gx::core::GxSysResource`, `gx::core::GxSysFile`, `gx::core::GxStreamReader`, `gx::core::GxResBase`
  - `gx::sound::GxResourceSeReleaseCallback`, `gx::sound::GxResourceSeResistCallback`
  - `gx::cri::GxSysCriSofdec2`, `gx::cri::GxSysCriAdx2` (also listed in the audit)
- **Error and diagnostic strings:** `createResourceRoutine failed:0/1`, `loadResourceSync failed:1/2`, `file open Error:0`, `file open Error:`, `Install Same File ! [%s.acb]`, `mmInstFileACB : Load File [%s.acb]`.

### 3.2 Interpretation
**STRONG HYPOTHESIS:** packed-asset loading is implemented in the **Gx framework's core resource system** (`gx::core`). In outline:
- `GxSysResourceArchive` opens `GxArchivedTable###.csh` / `GxArchivedFile###.dat`.
- `GxSysResource` and `GxSysResourceRegister` map type tags to `GxRes*` classes.
- Async loader threads stream data.
- A zlib read stream (`GxZlibReadStream`) handles the `BILZ`-wrapped zlib payloads.

This rests on: the names sitting together in one `.rdata` cluster; the `gx::core` RTTI; the tag-to-class table layout; and the embedded zlib (§4). It is **not proven**: no code was analysed.

---

## 4. Texture, model, shader, audio, video and loading references

| Area | CONFIRMED static evidence | Interpretation |
|---|---|---|
| **Compression** | zlib **1.2.3** strings ("deflate 1.2.3 Copyright 1995-2005 Jean-loup Gailly", "inflate 1.2.3 Copyright 1995-2005 Mark Adler", "incorrect header check", "unknown compression method", …); class `GxZlibReadStream` | STRONG HYPOTHESIS: the `BILZ` streams (headers `78 DA`, see `STEAM_ARCHIVE_STRUCTURE.md`) are inflated by this embedded zlib |
| **Textures** | `GxResTexture{Bmp,Dds,Png,Pvr,Sdds,Tga}`, extensions `png`, `pvr`, `tga`; `tex` tag; paths such as `SYSTEM/CH_Logo.tex`; folder prefixes `TEXTURE`, `TEXTURE_EN`, `TEXTURE_CN`, `TEXTURE_KR` | HYPOTHESIS: `tex` resources wrap one of these image formats. The texture format actually used in the PC archives is UNKNOWN. A `Pvr` (PowerVR) texture class exists in the engine; whether any PC asset uses it is UNKNOWN |
| **Models** | `GxResModel`, `GxResModelConfig(Ext)`, `GxMeshFile`, `GxMeshGroupFile`, `GxMaterialFile`, `GxResPrimitiveModel`; `mdl`/`mdc` tags; path prefix `TTM/dunmdl/…` | Engine-native model formats; internals UNKNOWN |
| **Shaders** | `GxResShader{Vertex,Pixel,VO,PO,GO,EO,CO,Package}`, `vsb`/`psb`/`spk` tags, `GxShaderPackage`, `GxSysShader`; import `D3DReflect` (D3DCOMPILER_47); strings `…__ps_5_0` with a YEBIS development path (`c:\workspace\yebis_draft\…`); `GxMovie/shader_4_0/…`, `GxMovie/shader_5_0/…` | STRONG HYPOTHESIS: shaders ship precompiled in `vsb`/`psb`/`spk` resources. **Correction to `TECHNICAL_AUDIT.md` (shader model UNKNOWN):** `ps_5_0` *does* appear (in YEBIS shader names). The audit's search used word boundaries that a preceding `_` defeats. Whether all game shaders target SM 5.0 is still UNKNOWN |
| **Audio (Gx/CRI)** | `GxSysCriAdx2`, `GxModCriAdx2Player`, `GxUnitCriAtomStream`, `GxResCriAtomCueSheetBinary` (`acb`), `GxResCriAtomConfiguration` (`acf`), `GxSysCriFs`; imports XAudio2_9Redist, DSOUND | CRI-ADX2-style interfaces exist in the Gx layer (audit) |
| **Audio (other layer)** | Ogg/Vorbis strings: `ogg_sync_pageout failed`, `ogg_stream_init failed`, `vorbis_synthesis_headerin failed`, `Vorbis eof`, `LOOPDEFS`, `OggS`, `vorbis`; nearby `AcbWaveform`, `.wav`, `prSample::Load(`; extension list `.hca .ogg .usm .mp4 .mkv` beside a path `d:\projects\maryskelterf\…`; `mmInstFileACB` messages | STRONG HYPOTHESIS: an Ogg Vorbis decoder is linked, consistent with the `.ogg` entries in the `.bra` archives. HYPOTHESIS: these strings belong to a **separate, non-Gx-prefixed layer** (prefixes `pr…`, `mm…`, project path `maryskelterf`) that substitutes Ogg/MP4 for CRI HCA/USM formats on PC. Its owner is UNKNOWN |
| **BRA archives** | Literal paths `masterWindows\Sound.bra`, `masterWindows\Voice_IL.bra`, `masterWindows\Sound_EN.bra` (with `%s %s`); development paths `../../resource/finalizedWin64/SOUND.cpk`, `SOUND_EN.cpk`, `VOICE_IL.cpk`, `MOVIE_EN.cpk`, `MOVIE_LOGO.cpk` | STRONG HYPOTHESIS: the build pipeline produced **CRI CPK** packs (`*.cpk`), and the shipped PC build replaces them with `.bra` archives (SOUND → Sound.bra, SOUND_EN → Sound_EN.bra, VOICE_IL → Voice_IL.bra) and MP4 folders (MOVIE_EN, MOVIE_LOGO). Which component reads `.bra`: UNKNOWN (no `PDA`/`BRA` class name found) |
| **Video** | Format strings `masterWindows/MOVIE_JP/%06u.mp4`, `…/MOVIE_KR/%06u.mp4`, `…/MOVIE_CN/%06u.mp4`, `…/MOVIE_EN/%06u.mp4`, and `masterWindows/MOVIE_LOGO/%s` (plus `%s` forms for other languages); `GxSysMovie`, `GxMovie`, `GxModSofdecMovie`, `GxSofdecMovieDriver`, `GxSysCriSofdec2`, shader names `GxMovie/shader_5_0/sofdec_yuv_forH264`; imports MF/MFPlat/EVR/dxva2/d3d9 | CONFIRMED: path templates match the shipped six-digit MP4 names; only `MOVIE_EN` and `MOVIE_LOGO` exist in this install. STRONG HYPOTHESIS: movies play through Media Foundation (H.264 MP4) behind a Sofdec-named Gx interface |
| **Languages and regions** | Templates and prefixes for JP / KR / CN / EN (movies, textures, fonts `sysfont_jp/en/cn.ffu`, scripts `SCRIPT_NA` / `SCRIPT_AS`, text `TextDataJp.bin` / `TextDataEn.bin`) | HYPOTHESIS: the parallel DAT sets (062–069 / 070–078 / 079–086) are language or region variants. Not verified |
| **"IL" content** | `Voice_IL.bra`, `VOICE_IL.cpk`, `TTM/data/eventIL.enc`, `TTM/data/tableIL.enc`, `tableIL_En.enc` | CONFIRMED that "IL" names a recurring content set; its meaning is UNKNOWN |

---

## 5. Windows and platform dependencies relevant to loading

### 5.1 File and threading imports (CONFIRMED, from the static import table)
- **File I/O (KERNEL32):** `CreateFileA`, `CreateFileW`, `ReadFile`, **`ReadFileEx`** (asynchronous, overlapped), `WriteFile`, `SetFilePointer`, `SetFilePointerEx`, `GetFileSize`, `GetFileType`, `GetFileAttributesA`, `GetFileAttributesExA`, `FindFirstFileExA`, `FindNextFileA`, `FindClose`, `CreateDirectoryA`, `CreateDirectoryW`, `FlushFileBuffers`, `SetEndOfFile`, `GetCurrentDirectoryA`, `GetPrivateProfileStringA`/`IntA`, `WritePrivateProfileStringA`.
- **Not in the static import table:** `CreateFileMappingA/W` and `MapViewOfFile`. Dynamic resolution through `GetProcAddress`/`LoadLibraryA` is still possible, so actual use is UNKNOWN.
- **Threading:** `CreateThread`, `SetThreadPriority`, `SetThreadAffinityMask`, `SuspendThread`, `ResumeThread`, events, mutexes, semaphores, critical sections, `WaitForMultipleObjects`.
- **Memory:** `VirtualAlloc`/`VirtualFree`, `HeapAlloc`/`HeapReAlloc`/`HeapFree`.
- **Other:** `SHGetFolderPathW` (SHELL32) and `GetFileVersionInfoA` (VERSION).

**STRONG HYPOTHESIS:** archives are read through regular (and possibly overlapped, via `ReadFileEx`) Win32 file I/O, driven by the `GxAsyncLoadThread*` classes. Memory-mapped archive access isn't indicated by the static imports.

### 5.2 Other platform components (CONFIRMED imports, also in the audit)
- **Graphics:** Direct3D 11 (`D3D11CreateDeviceAndSwapChain`), DXGI, `D3DReflect`.
- **Video presentation:** D3D9Ex, D3DX9 and DXVA2/EVR.
- **Video decode:** Media Foundation (MF, MFPlat).
- **Audio and input:** XAudio2 9, DirectSound, DirectInput 8, XInput 1.4.
- **Platform services:** Steamworks (9 functions), WSOCK32.
- **Dynamically loaded:** `libScePad.dll` (audit).

No CRI DLL is imported; the CRI-related code is statically linked or wrapped, as the audit recorded.

---

## 6. Which component handles archive loading?

| Archive type | Most likely handler | Evidence | Label |
|---|---|---|---|
| `GxArchivedFile###.dat` + `GxArchivedTable###.csh` | **Gx framework core resource system** (`gx::core`: `GxSysResourceArchive`, `GxSysResource`, `GxSysResourceRegister`, async loaders, `GxZlibReadStream`) | Name cluster (§1); tag-to-class registration layout (§2.2); `gx::core` RTTI (§3.1); embedded zlib (§4) | **STRONG HYPOTHESIS** |
| `Sound.bra`, `Sound_EN.bra`, `Voice_IL.bra` | **A different, non-Gx-named layer**, possibly port-specific, that substitutes the original CRI CPK sound packs | Literal `.bra` paths beside non-Gx strings; CPK development paths with matching names; Ogg/Vorbis decoder strings; `pr…`/`mm…` prefixes; no `PDA`/BRA class name | **HYPOTHESIS** |
| `MOVIE_*/%06u.mp4` | Gx movie interface (`GxSysMovie`, Sofdec-named) over **Media Foundation** | Path templates; MF imports; `sofdec_yuv_forH264` shader names | **STRONG HYPOTHESIS** |

**What can't be established statically:** call graphs, the order of archive opening, how name hashes or IDs are looked up in the 28-byte CSH records, and whether the "original/finalized" roots are ever used. All of these would need analysis of the code section, which has very high entropy (encryption or packing unverified) and is outside the permitted scope.

---

## 7. Implications for a Vita-compatible implementation (INFERRED)
These follow from the evidence above together with `STEAM_ARCHIVE_STRUCTURE.md` and `VITA_PERFORMANCE_BUDGET.md`. They aren't measurements.

| Requirement | Status |
|---|---|
| A reader for `GDAT` indexes and `.csh` tables | The container layout is known (`STEAM_ARCHIVE_STRUCTURE.md`); **the 28-byte record semantics are UNKNOWN** |
| zlib inflate | Portable C library; not currently installed in our VitaSDK (`MARY_SKELTER_VITA_FEASIBILITY.md` §4) |
| Decoders for every `GxRes*` type (textures, models, shaders, motions, collision, soft-body, fonts, `ResEnc`/`ResDat`/`ResBin`…) | **Formats UNKNOWN**; would need rights-holder documentation or source |
| Shaders | Shipped shader binaries are Direct3D-oriented (`D3DReflect`, `ps_5_0` in YEBIS). **They can't run on the Vita**; they would need source and a rewrite (`MARY_SKELTER_VITA_FEASIBILITY.md` §3) |
| Audio | Ogg Vorbis decoding (portable library, not installed) and a BRA reader (layout known) |
| Video | 1080p H.264 MP4 would need re-encoding for the Vita's decoder limits (unmeasured) |
| Asynchronous loading | Win32 overlapped I/O and threads → replaced with Vita (`sceIo`, threads) or SDL equivalents |
| Resource registration (tag → class) | Requires the engine's code; the tag list itself is now known (§2.2) |

**Conclusion (INFERRED):** static evidence identifies **where** loading happens (mainly the Gx core resource system) and **what** must be replaced. It doesn't provide the asset formats or the engine logic needed to implement them. Those still depend on rights-holder material (`MARY_SKELTER_VITA_FEASIBILITY.md` §9, §12).

---

## 8. Unresolved questions
1. How names are built and lookups work: how `GxArchivedFile`/`GxArchivedTable`, `%03d` and the extensions combine, and what the 28-byte `.csh` records contain.
2. Whether `GDAT`/`BILZ`/`PDA` are validated at runtime (no literals present).
3. Which component reads `.bra` archives, and who wrote the non-Gx (`pr…`/`mm…`) audio layer.
4. Whether the `original` / `finalizedCommon` / `finalizedNative` / `master` roots are used in the shipped build.
5. The meaning of `CL3`, `tid`, `enc`, `ffu`, `olf`, and of "IL" content.
6. Whether the parallel DAT sets are language or region variants.
7. The texture formats actually used inside `tex` resources on PC.
8. The full shader model set (`ps_5_0` confirmed only for YEBIS shader names).
9. Whether file mapping or other APIs are resolved dynamically.

---

## 9. Files inspected
- `C:\Program Files (x86)\Steam\steamapps\common\Mary Skelter Finale\MarySkelterFinale.exe`: read-only byte analysis.
- `C:\Program Files (x86)\Steam\steamapps\common\Mary Skelter Finale\masterWindows\GxArchivedFile051–088.dat` (37 files): **index tables only**, for the gap check behind the `STEAM_ARCHIVE_STRUCTURE.md` correction.
- `…\masterWindows\Sound.bra`, `Sound_EN.bra`, `Voice_IL.bra`: **header and directory only**, for the same gap check.
- `TECHNICAL_AUDIT.md`: existing findings, used as context and not modified.
- `STEAM_ARCHIVE_STRUCTURE.md`: corrected in this phase (precision fixes only).

## 10. Integrity
- **Steam installation:** a before/after snapshot of all 288 entries (path, size, last-write time, attributes) shows no differences, and the executable SHA-256 still matches the audit. The full result is in the session's final report.
- **Vita project:** no source, build file, benchmark data or existing committed document was modified. No commit was made.
