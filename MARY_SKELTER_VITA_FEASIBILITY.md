# Mary Skelter Finale on PS Vita: Technical Feasibility Investigation

**Milestone:** Phase 1, feasibility investigation (research only)  
**Date:** 2026-11-14  
**Repository state at writing:** `12270f2` (branch `main`), working Vita foundation unchanged  
**Inputs:** `TECHNICAL_AUDIT.md`, `TECHNICAL_BASELINE.md`, `GRAPHICS_TEST_RESULTS.md`, a new read-only string search of the installed game executable, the installed VitaSDK, and public web sources (listed in §14).

### Labels
| Label | Meaning |
|---|---|
| **CONFIRMED** | Directly observed: in the installed game files, the installed VitaSDK, our Vita app on hardware, or an official/primary public source. Each one names its source. |
| **INFERRED** | A reasoned conclusion from confirmed evidence, or a widely published public fact (for example Vita hardware specs) that this project hasn't verified itself. |
| **UNKNOWN** | Can't be determined from what we have. |
| **REQUIRES RIGHTS-HOLDER CONFIRMATION** | Only Idea Factory / Compile Heart, or a licensor such as Silicon Studio, CRI or Sony, can answer or authorise it. |

"Audit" below means `TECHNICAL_AUDIT.md` (2026-11-14). Findings taken from it are **not** re-verified as new facts unless stated.

### Scope and method
- **Game install:** only read. Nothing was executed, extracted, decrypted or copied. No game assets are in this project.
- **New inspection this milestone:** one read-only text search of `MarySkelterFinale.exe` for engine and middleware vendor names. Its SHA-256 still matches the audit.
- **Not attempted:** reading inside the game's archives, inspecting the DRM wrapper, or contacting anyone.

---

## 1. Project objective
Determine whether **the actual Mary Skelter Finale** (the PC release, built 2023-08-31) could realistically be adapted to run on a modded PS Vita, and what that would require. This is **not** about making a different or replacement game.

Our Vita work so far (`TECHNICAL_BASELINE.md`) is original engine-foundation code. It contains no Mary Skelter code or assets, and it can't run Finale's code or data as it stands.

---

## 2. Original-game technical findings

### 2.1 From the earlier audit (not re-verified unless noted)
| Finding | Label |
|---|---|
| Windows x86-64 executable, MSVC 14.16, built 2023-08-31, SHA-256 `5C16ABFD…` | CONFIRMED (audit; hash re-checked today) |
| Main renderer Direct3D 11 (`D3D11CreateDeviceAndSwapChain`, hull/domain shader classes) | CONFIRMED (audit) |
| D3D9Ex + DXVA2 + EVR + Media Foundation for .mp4 video | CONFIRMED (audit, imports) |
| Audio: XAudio2 9, DirectSound, CRI ADX2/Atom (`GxSysCriAdx2`) | CONFIRMED (audit) |
| Video middleware: CRI Sofdec2 (`GxSysCriSofdec2`); CRI File System (`GxSysCriFs`) | CONFIRMED (audit) |
| Input: DirectInput 8, XInput 1.4, `libScePad.dll` loaded at runtime | CONFIRMED (audit) |
| Platform: Steamworks; `.bind` section consistent with SteamStub DRM | CONFIRMED imports; DRM INFERRED (audit) |
| 458 `Gx*` identifiers and a `gx::` namespace tree | CONFIRMED (audit) |
| Data in proprietary `GxArchivedFile*.dat` (`GDAT`), `.csh` indexes and `.bra` files; 26 .mp4 videos; 9.08 GB install | CONFIRMED (audit) |
| No source code, shader source or debug symbols in the install | CONFIRMED (audit) |
| Shader model | UNKNOWN (audit could not confirm) |

### 2.2 New in this investigation (read-only string search)
| Finding | Evidence | Label |
|---|---|---|
| The executable references **OROCHI** by name | `SYSTEM/Orochi_Logo.tex`, `SYSTEM/Orochi_Logo` listed beside `CH_Logo`, `IF_Logo` and `GL_Logo` in a `LogoStage` list; C++ class `SW_OrochiThread`; identifier `MK_OROCHI_RESOURCE_ONE` | **CONFIRMED** (strings present) |
| The game is built with Silicon Studio's OROCHI | Names, a logo resource in the startup logo list, and a class beside `gx::core::GxThread` | **INFERRED (strong)** |
| The OROCHI **version** is 4 | No version string found | **UNKNOWN** |
| Silicon Studio's **YEBIS** post-processing middleware is linked, with a D3D11 backend | `YEBIS_D3D11`; "Yebis Depth Factor" and "Yebis Composite" parameters inside a `GxUnitFilterBase` filter; string `SiliconStudio Inc.` | **CONFIRMED** (strings present) |
| **Bullet** physics is used inside the Gx dynamics and collision modules | `_pBulletWorld`, `_pBulletDispatcher`, `_pBulletBroadphase` in `GxModDynamics` / `GxModCollision` | **CONFIRMED** (strings present) |
| PC asset paths share Nintendo Switch naming | Path `TEXTURE_NX_EN/Logo/…` ("NX" is the common Switch codename) | **INFERRED**: the PC build is likely derived from the Switch version |
| "Gx" is OROCHI's own framework, or Compile Heart's layer on top of it | Both appear together; no attribution string | **UNKNOWN** |

### 2.3 Public sources
| Finding | Label |
|---|---|
| Finale was developed by Compile Heart for PS4 and Switch, and later released on PC | CONFIRMED (public sources, §14) |
| Silicon Studio's 2019 release lists OROCHI platforms including **PlayStation Vita**, PS4, Switch, Xbox One, arcade and Windows | CONFIRMED (Silicon Studio press release via search, §14) |
| In OROCHI 4, PS3/Vita builds use the **older built-in graphics engine** and PS4 uses the **Mizuchi** renderer | CONFIRMED (reported by 4Gamer, §14) |
| Idea Factory / Compile Heart titles (Megadimension Neptunia VIIR, Death end re;Quest) were announced as using OROCHI 4 + Mizuchi | CONFIRMED (Siliconera, §14) |
| **Finale specifically** uses OROCHI 4 / Mizuchi, officially | **UNKNOWN**: no official statement found. The binary evidence (§2.2) is strong but unofficial. → **REQUIRES RIGHTS-HOLDER CONFIRMATION** |
| Mary Skelter: Nightmares (series entry 1) shipped on PS Vita in 2016 (JP) and 2017 (West); Mary Skelter 2 and Finale did not ship on Vita | CONFIRMED (public sources, §14) |
| Silicon Studio still operates and released YEBIS 4 in July 2025; no OROCHI licensing news since about 2019 found | CONFIRMED (press releases) / OROCHI current status **UNKNOWN** |
| Sony stopped accepting new PS Vita titles on the PlayStation Store on **2021-07-20** | CONFIRMED (multiple news sources, §14) |

---

## 3. Graphics compatibility analysis

| Finale (PC) | Vita / our stack | Assessment | Label |
|---|---|---|---|
| Direct3D 11 renderer | No D3D on Vita. vitaGL implements **OpenGL ES 2.0** plus fixed-function emulation over GXM (`OpenGL ES 2.0 VitaGL` string) | Complete renderer replacement or back-end swap | CONFIRMED (both sides) |
| Hull/domain (tessellation) shader classes | vitaGL has **no** `glPatchParameteri` and **no** `glDispatchCompute` | Tessellation and compute paths must be removed | CONFIRMED (symbols absent) |
| `GxSysGpgpu` (compute) | No compute API in vitaGL | Must be removed or replaced on the CPU | CONFIRMED absent / impact INFERRED |
| Precompiled D3D shaders (`D3DReflect` only) | vitaGL compiles **GLSL/Cg source** at runtime (`glShaderSource`, `glCompileShader` via `libshacccg`) or takes GXP binaries (`glShaderBinary`) | Every shader must be rewritten from source; D3D bytecode is unusable | CONFIRMED (APIs) / rewrite INFERRED |
| YEBIS (D3D11) post-processing: DoF, glare, motion blur, AA | YEBIS for Vita: none found in Silicon Studio's materials (YEBIS 4 lists Switch, Switch 2, PS5, Xbox Series, Windows) | Remove, or reimplement a much cheaper version | Vita version **UNKNOWN** → **REQUIRES RIGHTS-HOLDER / Silicon Studio confirmation** |
| Probable Mizuchi renderer (PS4-class) | OROCHI 4 on Vita uses the **older built-in** renderer, not Mizuchi (4Gamer) | Content made for Mizuchi would need re-authoring for the older renderer | INFERRED |
| Assets authored for PS4/Switch-class GPUs (polygon counts, texture sizes, effects) | Our measured scene: 488 triangles, 3 draw calls, about 256 KB of textures; **no FPS measurements recorded** | The Vita budget for Finale-class scenes is unmeasured; large asset reduction expected | Asset sizes **UNKNOWN**; reduction INFERRED |
| 1920×1080 configured | 960×544 native | UI and HUD layout rework | CONFIRMED (both) |
| MSAA on PC (AA parameters) | MSAA 2x and 4x builds install and run on our Vita; cost unmeasured | MSAA is available; budget unknown | CONFIRMED (observed) / cost UNKNOWN |
| Textures | RGBA8 and RGB565 both work in our app; vitaGL also defines DXT, PVRTC and ETC1 formats | Compressed formats need hardware testing | RGBA8/565 CONFIRMED (observed); compressed **UNKNOWN** |

**Graphics conclusion:** with source code and assets, the graphics would be a **re-platforming plus content downscale**, not a recompile. Without source, it's impossible. **INFERRED**

---

## 4. Audio and input compatibility

### Audio
| Finale (PC) | Vita | Assessment | Label |
|---|---|---|---|
| CRI ADX2/Atom with CRI-format audio inside the archives | CRI middleware historically supported Vita, but Vita builds come only through CRI under licence. The VitaSDK has `SceAudio` (output) and `SceAudiodec` (codecs `AT9`, `AAC`, `MP3`, `CELP`), and a `SceNgs` stub (no header) | Either a licensed CRI Vita runtime, or every sound converted and the audio layer rewritten | VitaSDK side CONFIRMED; CRI Vita availability **REQUIRES RIGHTS-HOLDER / CRI CONFIRMATION** |
| XAudio2, DirectSound | Not available | Replace | CONFIRMED |
| Audio data format inside the archives | Not inspected (proprietary) | — | UNKNOWN |
| No Ogg/Opus/MP3 decoder libraries installed in our VitaSDK | Only Sony stubs | A third-party decoder would need installing or writing for non-Sony formats | CONFIRMED |

### Video
| Finale (PC) | Vita | Assessment | Label |
|---|---|---|---|
| 26 .mp4 files (about 2.1 GB) via Media Foundation; CRI Sofdec2 also linked | `SceAvPlayer` and `SceVideodec` stubs and headers exist in the VitaSDK | Re-encoding is likely needed for size and decoder limits | APIs CONFIRMED; Vita decoder limits and the source videos' codec/resolution **UNKNOWN** |

### Input
| Finale (PC) | Vita | Assessment | Label |
|---|---|---|---|
| DirectInput, XInput, scePad (`GxPadPc`, `GxPadXbox`, `GxPadPs4`) | `SceCtrl`, `SceTouch`, `SceMotion`; SDL2 GameController (working in our app) | Most portable area: map to Vita buttons and sticks | CONFIRMED (our hardware tests for sticks and buttons) / effort INFERRED low |
| `GxTouch` classes | Vita front and rear touch | Possible fit; use unknown | INFERRED |
| Steamworks | Not applicable | Remove | CONFIRMED |

---

## 5. Memory and performance constraints

| Item | Finale | Vita | Label |
|---|---|---|---|
| Main RAM | Not measured; Switch target hardware has 4 GB | 512 MB main RAM + 128 MB video RAM | Vita: INFERRED (public specs); Finale's usage **UNKNOWN** |
| CPU | x86-64 PC; Switch ARM64 | 32-bit ARMv7 Cortex-A9, 4 cores (some reserved by the OS), roughly 333–444 MHz | INFERRED (public specs) |
| GPU | D3D11-class; PS4/Switch-class content | SGX543MP4+, no tessellation or compute in our API | INFERRED (public specs) / API limits CONFIRMED |
| Install size | 9.08 GB | A modded Vita can use large storage (memory card or SD adapters); size alone isn't the blocker | Finale CONFIRMED (audit); Vita storage INFERRED |
| File system | CRI FS and Gx archives | `SceIofilemgr` (`ux0:` etc.); readable only with a format specification | API CONFIRMED; archive format **UNKNOWN** → REQUIRES RIGHTS-HOLDER CONFIRMATION |
| 64-bit to 32-bit | The PC (x64) and likely Switch (ARM64) builds are 64-bit | The Vita is 32-bit | A source port must fix pointer-size assumptions | INFERRED |
| Our measured performance | — | No FPS, frame-time, RAM or VRAM numbers recorded; owner reports "smooth" with vsync on | CONFIRMED (absence of data) |

**Memory conclusion:** the Vita has roughly **1/8 of the Switch's RAM**. Fitting Finale's dungeons, characters, effects and UI would need a content-level reduction that only someone with the source assets could carry out. **INFERRED**

---

## 6. Potential engine and middleware obstacles

| Component | Obstacle | Label |
|---|---|---|
| **OROCHI (Silicon Studio)** | Use is strongly indicated by the binary. Its Vita back-end exists per Silicon Studio, but access needs a commercial licence; current availability of OROCHI, and especially its Vita back-end, is unknown | Use INFERRED; access **REQUIRES RIGHTS-HOLDER / Silicon Studio CONFIRMATION** |
| **OROCHI Vita back-end + homebrew toolchain** | Official Vita middleware is normally built against Sony's official Vita SDK. Whether those libraries could link or run under the homebrew VitaSDK is untested | **UNKNOWN** |
| **Gx framework** | Proprietary; no public documentation found | UNKNOWN → **REQUIRES RIGHTS-HOLDER CONFIRMATION** |
| **YEBIS** | No Vita version found in current materials | UNKNOWN |
| **CRI ADX2 / Sofdec2 / FS** | Commercial licences per platform | **REQUIRES RIGHTS-HOLDER / CRI CONFIRMATION** |
| **Bullet physics** | Open source (zlib licence); generally portable to ARM. The version and modifications used are unknown | INFERRED portable / version UNKNOWN |
| **Steamworks / DRM** | Not needed on Vita; the DRM-wrapped PC executable can't be the base for a port | CONFIRMED (audit) |
| **Sony distribution** | No new Vita titles accepted on PlayStation Store since 2021-07-20. An official commercial Vita release through PSN is not possible | CONFIRMED |
| **Homebrew distribution of a licensed title** | Releasing a licensed commercial game as homebrew would need explicit rights-holder agreement and isn't a standard publishing route | **REQUIRES RIGHTS-HOLDER CONFIRMATION** |

---

## 7. What our current Vita foundation already provides
All of this is original code (`TECHNICAL_BASELINE.md`, `GRAPHICS_TEST_RESULTS.md`).

| Capability | Status |
|---|---|
| Working VitaSDK + SDL2 + vitaGL build and VPK pipeline | CONFIRMED (hardware) |
| Known pitfall documented: SDL2 initialises vitaGL with legacy pool 0, so no immediate mode | CONFIRMED (hardware crash and fix) |
| 3D perspective rendering, depth, culling, textured indexed geometry | CONFIRMED (hardware) |
| Client-array and VBO paths both render correctly | CONFIRMED (observed on hardware) |
| RGBA8 and RGB565 textures, mipmaps, trilinear filtering | CONFIRMED (observed) |
| Fog, shader warm-up, vsync, MSAA 2x/4x builds run | CONFIRMED (observed); costs unmeasured |
| Analog input, first-person camera, grid collision | CONFIRMED (hardware) |
| HUD instrumentation (FPS, frame time, RAM/VRAM free) | CONFIRMED (present); readings not recorded |

**What it is not:** it isn't an emulator, translation layer or replacement for Finale's engine, and it can't load or run Finale's code or data. Its value for a real adaptation is as **(a) a measurement platform for Vita budgets** and **(b) a source of proven Vita integration knowledge**. **INFERRED**

---

## 8. What still needs to be developed (if a licensed adaptation went ahead)
Each item depends on rights-holder material.

| Work | Depends on | Label |
|---|---|---|
| Port or rebuild of the game code (Gx/OROCHI layer, game logic, scripts) for 32-bit ARM | Source code and engine licence | REQUIRES RIGHTS-HOLDER CONFIRMATION |
| Vita renderer back-end (vitaGL or GXM) replacing D3D11, Mizuchi and YEBIS | Engine source, shader source | REQUIRES RIGHTS-HOLDER CONFIRMATION |
| Rewrite of every shader to Cg/GLSL ES with Vita limits | Shader source | REQUIRES RIGHTS-HOLDER CONFIRMATION |
| Asset pipeline: decimate models, re-export textures (sizes, formats), re-encode video and audio | Original source assets and tools | REQUIRES RIGHTS-HOLDER CONFIRMATION |
| Audio and video layer (licensed CRI Vita, or replacements) | CRI licence or converted assets | REQUIRES RIGHTS-HOLDER CONFIRMATION |
| UI re-layout for 960×544 | UI source assets | REQUIRES RIGHTS-HOLDER CONFIRMATION |
| Memory budgeting and streaming for 512 MB | Measured data from the above | UNKNOWN |
| Input mapping to Vita | Code access | INFERRED low effort |
| Measured Vita performance budgets | **No rights needed**: can be done now with original synthetic content | Available now |

---

## 9. What requires official permission or documentation
- Permission to adapt the game at all, and on what terms
- The engine: confirmation of OROCHI (version), Gx documentation, and licence access to OROCHI and its Vita back-end
- CRI (ADX2, Sofdec2, FS) licensing for Vita
- YEBIS: whether a Vita path exists, or whether it can be removed
- Source code, shader source, original assets and build tools
- Archive format specifications (`GDAT`, `.csh`, `.bra`)
- Distribution: whether any release channel is acceptable, given that the PSN Vita store is closed to new titles
- Trademark and name use (the project folder still carries the title name; see audit §8)

**Label for every item above: REQUIRES RIGHTS-HOLDER CONFIRMATION**

---

## 10. Major technical risks

| # | Risk | Likelihood / impact | Label |
|---|---|---|---|
| R1 | No access to source, engine or assets: the project can't proceed toward Finale at all | Blocking | INFERRED (current state) |
| R2 | OROCHI's Vita back-end unavailable, unlicensable, or incompatible with the homebrew toolchain | High / blocking | UNKNOWN |
| R3 | Content made for PS4/Switch (Mizuchi, YEBIS) can't be reduced to Vita budgets without re-authoring most assets | High / very large cost | INFERRED |
| R4 | 512 MB RAM can't hold dungeon, party and effects at once without a redesigned streaming system | High | INFERRED |
| R5 | 64-bit to 32-bit porting defects across a large proprietary codebase | Medium | INFERRED |
| R6 | CRI middleware licences unavailable for Vita in 2026 | Medium / large rework | UNKNOWN |
| R7 | No official distribution route (PSN closed to new Vita titles since 2021) | High / commercial blocker | CONFIRMED |
| R8 | Our Vita performance budget is unmeasured, so all sizing is guesswork | Medium, fixable now | CONFIRMED (no data) |

---

## 11. Realistic development roadmap
Gated: each stage only starts if the previous gate passes.

| Stage | Work | Gate to proceed | Needs rights holder? |
|---|---|---|---|
| **0. Now** | Complete §3/§4 measurements in `GRAPHICS_TEST_RESULTS.md`; build a **Vita performance-budget benchmark** with original synthetic content (triangles/frame, draw calls, texture memory, fill rate, MSAA and VBO cost at 30/60 fps) | Measured budget table | No |
| **1. Rights-holder response** | Get answers to §12 | Written permission and a technical contact | **Yes** |
| **2. Engine access** | Confirm the OROCHI version and licence path; check whether an OROCHI Vita back-end can be built and run on a modded Vita | A small engine sample runs on Vita | **Yes** (plus Silicon Studio) |
| **3. Budget fit study** | Compare measured Vita budgets (stage 0) against a representative, legitimately provided dungeon or scene | The scene fits within budget after a defined reduction | **Yes** |
| **4. Vertical slice** | One dungeon floor, party, battle and save on Vita | Stable at target frame rate within memory | **Yes** |
| **5. Full adaptation** | Remaining content, audio/video, UI, QA | Release terms agreed | **Yes** |

Without stage 1, the project **stops at stage 0**. It can't advance toward Finale using only the PC release. **INFERRED**

---

## 12. Information needed from Idea Factory International
The owner reports a previous enquiry. No copy of that correspondence or any reply is stored in this project, so its status is **UNKNOWN**.

1. **Permission:** may we investigate and prototype a Vita adaptation of Mary Skelter Finale, and under what written terms (non-commercial / private, NDA)?
2. **Engine identity:** does Finale use Silicon Studio OROCHI? Which version? Is the `Gx` framework part of OROCHI or Compile Heart's own?
3. **Engine licensing:** could Idea Factory / Compile Heart grant access, or sponsor a request to Silicon Studio for OROCHI including any Vita back-end?
4. **Renderer:** does Finale use Mizuchi? Does a PS Vita or OROCHI-built-in-renderer path exist in their codebase?
5. **Middleware:** which CRI products and versions are used? Can CRI Vita runtimes be licensed for this purpose? Is YEBIS required, or can it be removed?
6. **Source and assets:** would source code, shader source, original source assets and build tools be available under NDA?
7. **Data formats:** documentation for `GDAT` / `.csh` / `.bra` archives and the audio and video formats.
8. **Technical baseline:** the Switch version's memory budget and asset sizes, as a reference for Vita reduction.
9. **Distribution:** with the PSN Vita store closed to new titles, is any distribution of a Vita build acceptable to them (for example private test builds only)?
10. **Branding:** may the project use the Mary Skelter name, and if so, how?

**Label for all: REQUIRES RIGHTS-HOLDER CONFIRMATION**

---

## 13. Overall feasibility assessment

| Question | Assessment | Label |
|---|---|---|
| Can the existing PC release be ported directly, without source? | **No.** x86-64, D3D11 and YEBIS-D3D11 binaries, DRM-wrapped, proprietary archives; nothing reusable on ARMv7 Vita | CONFIRMED (audit) / conclusion INFERRED |
| With full rights-holder cooperation (source, engine and middleware licences, assets), is a Vita adaptation technically possible? | **Possibly, but not shown.** OROCHI has a Vita back-end and the series ran on Vita before, but Finale's content targets PS4/Switch-class hardware with about 8× the Vita's RAM. It would be a major re-authoring effort, closer to a remake for weaker hardware than a port | INFERRED, with key unknowns R2, R4, R6 |
| Is it realistic for this project today? | **No.** Every path past stage 0 depends on rights-holder material that we don't have | INFERRED |
| Is there an official release path? | **No** PSN route (closed to new Vita titles since 2021) | CONFIRMED |
| What is achievable now without permission? | Measuring the Vita's real performance and memory budgets with original content, to answer R8 and prepare evidence for any discussion with the rights holder | INFERRED |

**Bottom line:** the evidence does **not** support saying a Mary Skelter Finale Vita port is possible for this project. Its feasibility depends on rights-holder access to code, engine, middleware and assets, followed by a large content reduction. The only productive technical work available without permission is measuring Vita budgets with original content.

---

## 14. Sources
**Public web sources**, consulted 2026-11-14 through search result summaries; individual pages were not archived:
- Silicon Studio, OROCHI press release (2015): <https://www.siliconstudio.co.jp/news/pressreleases/2015/pdf/20150617_orochi.pdf>
- Silicon Studio, 2019 release (Left Alive), lists OROCHI platforms: <https://www.siliconstudio.co.jp/en/news/pressreleases/2019/en1901leftalive/en1901leftalive.html>
- 4Gamer, OROCHI 4 / Mizuchi and built-in renderer for PS3/Vita: <https://www.4gamer.net/games/032/G003263/20150718006/>
- Game Developer, "Silicon Studio's Orochi game engine updated with PS Vita support": <https://gamedeveloper.com/business/silicon-studio-s-orochi-game-engine-updated-with-ps-vita-support>
- Siliconera, OROCHI 4 and Mizuchi to be used in Idea Factory games: <https://www.siliconera.com/orochi-4-mizuchi-rendering-engine-used-idea-factory-games/>
- Silicon Studio, YEBIS 4 release (July 2025): <https://www.siliconstudio.co.jp/en/news/pressreleases/2025/250718yebis4/250718yebis4.html>
- YEBIS licensing page: <https://www.siliconstudio.co.jp/middleware/yebis3/en/requirements_license/>
- Anime News Network, Mary Skelter Finale announced for PS4/Switch: <https://www.animenewsnetwork.com/news/2020-03-28/mary-skelter-finale-game-announced-for-ps4-switch/.158056>
- Wikipedia, Mary Skelter: Nightmares (Vita 2016): <https://en.wikipedia.org/wiki/Mary_Skelter:_Nightmares>
- Gematsu, Mary Skelter: Nightmares Western Vita launch: <https://www.gematsu.com/2017/08/mary-skelter-nightmares-launches-september-19-north-america-september-22-europe>
- Gematsu, Mary Skelter 2 announced for PS4: <https://gematsu.com/2018/03/mary-skelter-2-announced-ps4>
- Push Square, new PS Vita releases end 20 July 2021: <https://www.pushsquare.com/news/2021/07/new_ps_vita_releases_pegged_for_20th_july_as_sony_ceases_submissions>
- Game Rant, PS Vita store open but not accepting new games: <https://gamerant.com/playstation-vita-store-staying-open-not-accepting-new-vita-games/>

**Local sources:** `TECHNICAL_AUDIT.md`, `TECHNICAL_BASELINE.md`, `GRAPHICS_TEST_RESULTS.md`; a read-only string search of `MarySkelterFinale.exe` (SHA-256 `5C16ABFD…`); the installed VitaSDK (`/usr/local/vitasdk`, built 2026-08-25).
