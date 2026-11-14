# Technical Audit — Mary Skelter Finale (Steam, Windows)

**Date:** 2026-11-14  
**Subject install:** `C:\Program Files (x86)\Steam\steamapps\common\Mary Skelter Finale`  
**Scope:** Read-only inspection of file listings, file headers and the main executable's PE headers and import table.

### Method and constraints
- Nothing in the Steam folder was modified, moved, deleted or executed.
- No archives were extracted. Only the first 16 bytes of a few data files were read to identify their headers.
- The DRM wrapper was not inspected, analysed or bypassed.
- No game assets were copied into this project.

### Evidence labels used in this document
| Label | Meaning |
|---|---|
| **[VERIFIED]** | Observed directly in the installed files during this audit. |
| **[INFERRED]** | A reasoned conclusion from verified evidence, but not directly proven. |
| **[EXTERNAL]** | General knowledge from outside this install (platform specs, publishing history, tooling). It was not checked during this audit and should be confirmed before relying on it. |
| **[UNKNOWN]** | Not determined. |

---

## 1. Confirmed game executable information

| Field | Value | Status |
|---|---|---|
| File | `MarySkelterFinale.exe`, 11,393,304 bytes | [VERIFIED] |
| SHA-256 | `5C16ABFDC3F355404C2C7FAA6B277E5023812D2292CB8C5D4AD5177BA6C05A54` | [VERIFIED] |
| Format | PE32+ (optional header magic `0x20B`) | [VERIFIED] |
| Architecture | x86-64 (machine `0x8664`) | [VERIFIED] |
| Build timestamp | 2023-08-31 10:52:08 UTC | [VERIFIED] |
| Linker | MSVC 14.16 (Visual Studio 2017 toolset) | [VERIFIED] |
| Minimum OS / subsystem | 6.0 (Windows Vista or later), Windows GUI subsystem | [VERIFIED] |
| Version resource | Present but all fields empty (no company, product or version) | [VERIFIED] |
| Code signing | None (Authenticode directory size 0) | [VERIFIED] |
| Debug symbol path | `D:\Projects\MarySkelterF\MarySkelterF\resource\MarySkelterFinale.pdb`. The PDB itself is not shipped | [VERIFIED] |
| Sections | `.text .rdata .data .pdata _RDATA .rsrc .reloc .bind` | [VERIFIED] |
| `.bind` section | Consistent with the SteamStub DRM wrapper. Deliberately not inspected | [INFERRED] |

### Install layout (9.08 GB total) [VERIFIED]
```
Mary Skelter Finale\
├── MarySkelterFinale.exe
├── MarySkelterFinale.ini          settings: 1920x1080 @ 60 Hz, key bindings, DLCEnable=0, Version=v0.25
├── steam_api64.dll
├── xaudio2_9redist.dll
├── masterWindows\
│   ├── GxArchivedFile051–088.dat  37 files, ~5.26 GB, header tag "GDAT"
│   ├── GxArchivedTable0xx.csh     37 index tables, ~0.2 MB, header bytes 00 'hsc'
│   ├── *.bra                      3 files, ~1.35 GB
│   ├── MOVIE_EN\, MOVIE_LOGO\     26 .mp4 videos, ~2.1 GB
└── DELUXE\                        bonus content: art books (PDF), OST (MP3), wallpapers, desktop theme
```

The `.ini` value `Version=v0.25` is a settings-file field, not a confirmed engine or game version. [INFERRED]

---

## 2. Gx engine findings

### Verified
- The executable contains **458 unique identifiers beginning with `Gx`**. [VERIFIED]
- Distribution by family: `GxRes*` 57, `GxUnit*` 53, `GxSys*` 47, `GxMod*` 30, `GxParam*` 8, other 257+. [VERIFIED]
- Mangled C++ names show a `gx::` namespace tree. [VERIFIED] The namespaces are:
  `core`, `shader`, `filter`, `hud`, `motion`, `physics`, `sound`, `network`, `p2p`, `character`, `cri`.
- Representative subsystems found: [VERIFIED]
  - **Core / memory:** `GxCore`, `GxSysMain`, `GxAllocatorHeap`, `GxAllocatorFrame`, `GxClassBase`
  - **Rendering:** `GxSysRender`, `GxRenderDevice`, `GxRenderContext`, `GxRenderThread`, `GxRenderVertexShader`, `GxRenderPixelShader`, `GxRenderHullShader`, `GxRenderDomainShader`, `GxShaderPackage`, `GxSysGpgpu`
  - **Post-processing:** `GxDepthOfFieldParameter`, `GxGlareParameter`, `GxCameraMotionBlurParameter`, `GxAntiAliasParameter`
  - **Resources:** `GxSysResourceArchive`, `GxArchivedFile`, `GxArchivedTable`, `GxSysResourceAsyncLoadMulti`
  - **Simulation:** `GxSysDynamics`, `GxSysCollision`, `GxResSoftbodySimulation`, `GxResMotionIK`
  - **Input:** `GxPad`, `GxPadPc`, `GxPadPs4`, `GxPadXbox`, `GxTouch`, `GxVibration`
  - **Middleware wrappers:** `GxSysCriAdx2`, `GxSysCriSofdec2`, `GxSysCriFs`
  - **Other:** `GxSysNetwork`, `GxSysP2P`, `GxSysVRBase`, `GxSysUniformedVRBase`
- The data file names (`GxArchivedFile`, `GxArchivedTable`) match engine class names. The archives are therefore in the engine's own resource format. [VERIFIED]
- No Unity, Unreal or other known commercial engine runtime files or libraries are present. [VERIFIED]
- The string `Idea Factory` appears in the executable. [VERIFIED]

### Inferred / external
- "Gx" is a proprietary in-house engine, not a licensed third-party engine. [INFERRED]
- The engine targets multiple platforms, including PS4 (`GxPadPs4`, `scePad*` code paths). [INFERRED]
- The engine belongs to Compile Heart / Idea Factory. [EXTERNAL] The string "Compile Heart" does not appear in the binary.
- Engine version: [UNKNOWN]

---

## 3. Graphics, audio and input dependencies

### Statically imported DLLs (loaded at startup) [VERIFIED]
| Area | DLL | Imported functions (notable) |
|---|---|---|
| Graphics | `d3d11.dll` | `D3D11CreateDeviceAndSwapChain` |
| Graphics | `dxgi.dll` | `CreateDXGIFactory` |
| Graphics | `D3DCOMPILER_47.dll` | `D3DReflect` only. No compile functions are imported |
| Graphics / video | `d3d9.dll` | `Direct3DCreate9Ex` |
| Graphics / video | `d3dx9_43.dll` | `D3DXLoadSurfaceFromSurface` |
| Video | `MF.dll`, `MFPlat.DLL`, `EVR.dll`, `dxva2.dll` | Media session/topology, `MFStartup`, `MFCreateVideoSampleFromSurface`, `DXVA2CreateDirect3DDeviceManager9` |
| Audio | `XAudio2_9Redist.dll` | 3 ordinal imports. Shipped in the install |
| Audio | `DSOUND.dll` | 1 ordinal import |
| Input | `DINPUT8.dll` | `DirectInput8Create` |
| Input | `XINPUT1_4.dll` | 3 ordinal imports |
| Platform | `steam_api64.dll` | 9 functions incl. `SteamAPI_Init`, `SteamAPI_RunCallbacks` |
| Network | `WSOCK32.dll` | 17 ordinal imports |
| Text input | `IMM32.dll` | 13 IME functions |
| System | `KERNEL32`, `USER32`, `GDI32`, `ADVAPI32`, `SHELL32`, `ole32`, `OLEAUT32`, `WINMM`, `VERSION` | Standard Win32 |

No delay-load imports are present. [VERIFIED]

### Dynamically loaded [VERIFIED by strings]
- `libScePad.dll`, with function names `scePadInit`, `scePadOpen`, `scePadReadState`, `scePadSetLightBar`, `scePadSetVibration` and motion-sensor calls.
- This library is **not** in the install folder. [VERIFIED]
- It is most likely loaded optionally through `LoadLibrary`/`GetProcAddress` for PlayStation controller support. [INFERRED]

### Embedded middleware and libraries [VERIFIED by strings]
- **CRI ADX2 / Atom (audio):** `CriAtomStream`, `CriAtomCueSheetBinary`, `CriAtomConfiguration`, `Adx2Player`
- **CRI Sofdec2 (video):** `GxSysCriSofdec2`
- **CRI File System:** `GxSysCriFs`
- **zlib:** copyright strings for Jean-loup Gailly and Mark Adler

### Interpretation
- **Main renderer is Direct3D 11.** [VERIFIED] The evidence is the device-creation import, the `D3D11_ERROR_*` strings, and the hull and domain shader classes, which are features that require D3D11.
- **D3D9Ex is probably used only for video presentation.** [INFERRED] It appears alongside DXVA2/EVR, which is a common Media Foundation interop pattern.
- **Shaders are precompiled.** [INFERRED] Only `D3DReflect` is imported. Precompiled shaders would ship as DXBC bytecode inside the archives.
- **Shader model:** [UNKNOWN] An earlier claim of `ps_5_0` could not be confirmed with a strict search.
- **Settings keyboard ID:** the `.ini` controller GUID `6f1d2b60-d5a0-11cf-bfc7-444553540000` is DirectInput's `GUID_SysKeyboard`. [VERIFIED by known constant]

---

## 4. Known hardware compatibility challenges (Windows build → PS Vita)

Vita specifications below are [EXTERNAL] and should be confirmed against current documentation.

| Area | Windows build | PS Vita | Impact |
|---|---|---|---|
| CPU ISA | x86-64 [VERIFIED] | ARM Cortex-A9, 32-bit ARMv7 [EXTERNAL] | The binary cannot run on the Vita. Any port needs a full recompile from source |
| CPU budget | Desktop-class | 4 cores (some reserved by the OS), ~333–444 MHz [EXTERNAL] | Soft-body sims, IK, flocking and async loaders would need heavy cuts |
| Graphics API | Direct3D 11 [VERIFIED] | PowerVR SGX543MP4+ via GXM [EXTERNAL] | The whole renderer must be rewritten |
| Shader features | Hull/domain (tessellation) classes [VERIFIED], compute (`GxSysGpgpu`) [VERIFIED] | No tessellation, no compute shaders [EXTERNAL] | Those features must be removed. DXBC shaders cannot be used; they must be rewritten in Cg/GXP |
| Post-processing | DoF, glare, motion blur, AA [VERIFIED] | Limited fill rate and bandwidth [EXTERNAL] | Most effects must be cut or simplified |
| Memory | Desktop RAM / VRAM | 512 MB main RAM + 128 MB video RAM [EXTERNAL] | Assets must be rebuilt at lower resolution and polygon counts |
| Resolution | 1920x1080 configured [VERIFIED] | 960x544 display [EXTERNAL] | All UI and layout must be re-authored |
| Storage | 9.08 GB install [VERIFIED] | Memory cards / game cards with limited capacity [EXTERNAL] | Video (~2.1 GB) and asset archives (~6.6 GB) must be heavily recompressed |
| Video | H.264 .mp4 via Media Foundation [VERIFIED container] | Hardware decoder with its own resolution and profile limits [EXTERNAL] | All videos must be re-encoded. Video resolution and profile were not checked [UNKNOWN] |
| Audio | XAudio2 + CRI ADX2 [VERIFIED] | Different audio API; CRI needs a platform-specific licensed build [EXTERNAL] | The audio backend must be replaced |
| Input | DirectInput, XInput, scePad [VERIFIED] | Built-in controls, front touch, rear touchpad [EXTERNAL] | Input must be remapped. The engine already has a touch layer (`GxTouch`) [VERIFIED], though its use is [UNKNOWN] |
| Platform services | Steamworks [VERIFIED] | Not available | Steam calls must be removed |
| DRM | SteamStub-style `.bind` section [INFERRED] | N/A | Not to be touched. This is another reason the PC executable is not a valid porting base |

---

## 5. Missing source code and development resources

### Verified absent from the install [VERIFIED]
- No engine or game source code (`.c/.cpp/.h/.hpp`)
- No shader source code (`.hlsl/.fx`)
- No project or build files (`.sln/.vcxproj`)
- No debug symbols (`.pdb`). Only a path to the developer's machine is embedded
- No static libraries or object files
- No asset-pipeline tools, editors or exporters
- No documentation of the `GDAT` / `.csh` / `.bra` formats

The only text file in the whole install is `DELUXE\Theme\README.txt`, which belongs to the desktop theme.

### Resources a genuine port would require [INFERRED]
| Resource | Owner |
|---|---|
| Gx engine source, plus any existing Vita or ARM platform layer | Rights holder [EXTERNAL] |
| Game code, scripts and tooling | Rights holder |
| Original source assets (models, textures, audio stems) at authoring quality | Rights holder |
| CRI ADX2 / Sofdec2 / FS licences for PS Vita | CRI Middleware |
| Official PS Vita SDK, dev kits and publishing approval | Sony Interactive Entertainment |

---

## 6. Feasibility assessment — genuine port

**Conclusion: a genuine port is not feasible as a community or independent project.**

| Factor | Assessment |
|---|---|
| Recompiling from source | Impossible without the source code, which is absent [VERIFIED] |
| Using the existing executable | Not viable. It is x86-64 [VERIFIED], depends on D3D11 [VERIFIED], and is DRM-wrapped [INFERRED] |
| Reverse-engineering the engine and archives | Technically enormous and legally restricted (see §8). It is out of scope for this project by the user's instruction |
| Hardware fit | Poor even with source code. Tessellation and compute features plus a ~9 GB footprint would require a "demake", not a port [INFERRED from §4] |
| Official route | Possible only for the rights holder, using the official SDK, middleware licences and platform approval [EXTERNAL]. Sony's current acceptance of new Vita titles should be checked [EXTERNAL] |

### Historical context [EXTERNAL]
Earlier entries in the series were released on PS Vita by the rights holder. This shows the franchise has been on the hardware before. It does not mean this game's engine build or assets would fit.

---

## 7. Separate native Vita implementation roadmap

The legally viable path is a **separate, original project**: a native Vita game in the same broad genre (first-person dungeon-crawler RPG). It would be built from scratch with open-source homebrew tooling and **original** content.

### Ground rules
- No code, assets, archives or data from Mary Skelter Finale are used, copied, extracted or referenced. This includes art, models, audio, text, video, UI and maps.
- No recreation of the series' characters, names, story, setting, art style, music or level designs, even renamed. Genre mechanics (grid movement, party combat, dungeon mapping) are fine. The franchise's specific expression is not.
- The project gets its own working title, and the folder or repo name should eventually change to avoid implying an official port.

### Tooling (all [EXTERNAL], to be confirmed when we start)
- **Toolchain:** VitaSDK (open-source GCC/ARM toolchain and homebrew SDK)
- **Graphics:** vitaGL (OpenGL-style layer over GXM), or GXM directly
- **Platform layer:** SDL2 Vita port for input, audio and windowing
- **Testing:** real hardware running homebrew; emulator support (Vita3K) as a convenience
- **Assets:** original or properly licensed (CC0 / commercially licensed) art and audio

### Phases
| Phase | Goal | Exit criteria |
|---|---|---|
| 0. Design | Write an original design doc: setting, characters, core loop, scope sized for the Vita | Design doc approved. Nothing in it is derived from the source game |
| 1. Toolchain | Install VitaSDK and build a "hello triangle" VPK | Runs on hardware or emulator at 960x544 |
| 2. Engine core | Main loop, fixed timestep, memory arenas, file I/O, asset pack format of our own design | Stable 30/60 fps empty scene, memory budget tracked |
| 3. Renderer | Grid-based 3D corridors, texture atlases, simple lighting, sprite/2D UI layer | Dungeon walkable at target frame rate within budget |
| 4. Input | Buttons, analog sticks, front touch for menus, optional rear touch | All actions mapped. Touch is optional, never required |
| 5. Gameplay systems | Party, stats, turn-based combat, inventory, auto-map, save/load | Vertical slice: one floor, one battle, one save |
| 6. Audio / video | Music and SFX streaming (OGG/Opus or native ATRAC9 via SDK), short cutscenes if any | Audio within memory budget, no stutter |
| 7. Content | Original dungeons, enemies, story and UI art | Content-complete milestone |
| 8. Optimisation and QA | Profile CPU/GPU, reduce load times, test battery and suspend/resume | Stable on hardware, memory headroom ≥10% |
| 9. Release | Package as a homebrew VPK with a licence file and credits | Public build under its own name |

### Starting budgets (to be validated in Phase 2)
- **Frame rate:** 30 fps target
- **Memory:** ~350 MB main RAM for game data, ~96 MB video RAM for textures and render targets
- **Install size:** <1 GB

---

## 8. Legal and licensing considerations

*This is not legal advice. Consult a qualified lawyer before distributing anything.*

| Topic | Notes |
|---|---|
| Copyright in the game | The code, art, audio, text, video and characters are owned by the rights holder(s). The Steam licence grants personal play rights, not rights to redistribute, port or adapt [EXTERNAL] |
| Trademarks | "Mary Skelter" and related names and logos are likely registered trademarks. Do not use them in a project name, VPK title, icon or marketing [EXTERNAL] |
| Steam Subscriber Agreement / EULA | Typically prohibits reverse engineering, decompiling and creating derivative works [EXTERNAL]. Not reviewed in this audit |
| DRM circumvention | Bypassing technical protection measures can violate laws such as DMCA §1201 (US) and EU Copyright Directive Art. 6, regardless of intent [EXTERNAL]. The `.bind` section was deliberately not inspected |
| Proprietary archives | Extracting or converting `GDAT` / `.bra` content to run on other hardware would create unauthorised copies or derivative works [EXTERNAL] |
| Third-party middleware | CRI ADX2/Sofdec2/FS, Steamworks and Microsoft redistributables each carry their own licences [EXTERNAL]. None of their code may be reused |
| Homebrew SDK licences | VitaSDK, vitaGL and SDL2 carry open-source licences (various permissive / GPL-family) that must be honoured with attribution and licence files [EXTERNAL] |
| Original project | An original game in the same genre, with its own name and original content, avoids these issues. Note that close imitation of distinctive characters, story or art can still infringe |
| Official port | Only the rights holder (or a licensee) can produce one. If a Vita version is wanted, contacting the publisher is the legitimate route |

---

## Appendix — summary of verification status

| Claim | Status |
|---|---|
| x86-64 Windows PE, MSVC 14.16, built 2023-08-31 | VERIFIED |
| Proprietary "Gx" engine with `gx::` namespace, 458 identifiers | VERIFIED |
| Direct3D 11 is the main renderer | VERIFIED |
| D3D9Ex used only for video playback | INFERRED |
| Shaders precompiled; shader model 5.0 | INFERRED / UNKNOWN |
| CRI ADX2, Sofdec2, FS embedded | VERIFIED |
| SteamStub DRM wrapper | INFERRED (not inspected) |
| Engine also targets PS4 | INFERRED |
| Engine developer is Compile Heart | EXTERNAL |
| Engine version | UNKNOWN |
| Archive internals (`GDAT`, `.csh`, `.bra`) | UNKNOWN (header tag only) |
| Source code, PDB or tools in install | VERIFIED ABSENT |
| Vita hardware specs and homebrew tooling | EXTERNAL |
