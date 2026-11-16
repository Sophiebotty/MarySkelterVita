# Vita ResPipe: resource pipeline and the VRS1 format (Phase 10)

**Status:** approved specification (stage 10.1, approved on 2026-11-16). Implemented and tested on the PC so far: the stage 10.2 fixtures and independent checker, the stage 10.3 format checks (`src/resource/rs_format.c`) and the stage 10.4 loader (`src/resource/rs_loader.c`). No GL upload, Vita build or package exists yet.
**Format version:** VRS1 version 1, frozen since its approval on 2026-11-16 (see section 12).
**Application:** Vita ResPipe, title ID `VDNG00006`, version 01.00. It is separate from VDNG00001–00005, which stay unchanged.
**Content:** synthetic only. VRS1 is an original format made for this project. No Mary Skelter Finale file, format or asset is read, imitated or described here.

## 1. Scope
- **What ResPipe will do:**
  - load synthetic resources (textures, meshes, a level grid) from GDAT-style archives;
  - validate every layer of each entry;
  - upload them to vitaGL;
  - draw them with the existing renderer modules on a PS Vita, at a stable 30 fps.
- **Code it reuses:** the hardware-tested GDAT Proto reader and ContentFit's background reader, both unmodified (section 4.8).
- **What it cannot show:** passing tests show that this pipeline works with synthetic data on this Vita and this SDL2 + vitaGL stack. They say nothing about Finale's archives or assets.
- **The transport layout:** it follows observations recorded in `STEAM_ARCHIVE_STRUCTURE.md` (the GDAT index and the `BILZ` block), because the reused reader expects it. That does not make ResPipe compatible with real archives, and it is never used on them.
- **Files:**
  - reads come only from the app's own `app0:fixtures/`;
  - writes go only below `ux0:data/ResPipe/`: the run files and vitaGL's shader cache (`ux0:data/ResPipe/shader_cache`), redirected at link time as in ContentFit;
  - nothing is ever overwritten or deleted.

  `ux0:data/ResPipe/` is the owner's "ux0/ResPipe/", confirmed on 2026-11-15. Like every app in this project, ResPipe is built as safe homebrew (the VitaSDK default), and ContentFit's "ux0/ContentFit/" is `ux0:data/ContentFit/` in the same way.
- **Not covered here:**
  - the fixtures (stage 10.2);
  - the code (stages 10.3–10.6);
  - the Vita build (10.7) and hardware runs (10.8, 10.9). Each has its own approval gate.

## 2. Evidence labels
Every capability this design relies on carries one of three labels:

- **[M] measured:** observed on PS Vita hardware. The source is named.
- **[A] assumption:** not yet measured. The stage that will check it is named.
- **[D] design decision:** chosen for this project. The reason is given.

### 2.1 Measured on hardware [M]
Sources:
- **ContentFit:** run 001, 2026-11-15, 3 iterations, all 289 rows `OK`; see `CONTENTFIT_RESULTS.md`.
- **GDAT Proto:** Stage 3, 99/99 tests in each of 3 iterations.
- **Main app:** phase 5, `TECHNICAL_BASELINE.md` (items marked [HW] there).

| ID | Capability | Source |
|---|---|---|
| M1 | Free memory at start: vitaGL VRAM 83.9 of 96 MiB, vitaGL RAM 63.5 of 100 MiB, PHYCONT 26 of 26 MiB. The C library heap is a 128 MiB block with 57,680 bytes in use. Kernel USER_RW free: 12 MiB | ContentFit log |
| M2 | Reads from `app0:`: stdio 2.56–2.58 MiB/s at every chunk size from 16 KiB to 4 MiB; direct `sceIoRead` 9.34–9.73 MiB/s. From ux0: 2.86–2.87 and 9.81–9.82 MiB/s. 16 KiB reads take 1.63 ms each in order and 2.93–2.94 ms shuffled | ContentFit C1 |
| M3 | zlib 1.3.2 inflate on the main thread produces about 18.9 MiB/s of output (8 MiB in 424 ms, 64 MiB in 3,382 ms) | ContentFit C2 serial, iteration 1 breakdown |
| M4 | A worker thread reading regions with `sceIoPread` while the main thread inflates: 8 MiB in 0.63–0.64 s, 32 MiB in 2.47–2.48 s | ContentFit C2 pipelined (`cf_ring`, 3 slots) |
| M5 | A worker thread reading **and inflating** (`cf_ring`, 2 slots, CFP1 checks on the worker) while the main thread uploads, at locked 30 fps: no interval over 34 ms; 90 texture uploads per iteration, average 2.09–2.16 ms, slowest 6.24–6.66 ms | ContentFit C4-STRM |
| M6 | RGBA8, RGB565, DXT1, DXT5, ETC1 and PVRTC4 at 256×256 with full mip chains are accepted at every level, with rendering cost P95 5.65–5.71 ms. The measured VRAM use of each format scene matched its texture-plus-buffer byte count to within 2 KiB | ContentFit C3-T |
| M7 | Static vertex and index buffers drawn with `glDrawElements`: up to 100,800 triangles in 300 draws (Dungeon XL) at a cost P95 of 7.44–7.46 ms. All 17 synthetic scene tiers are stable at 30 fps | ContentFit C3 |
| M8 | Swap interval 2 gives a median frame interval of 33.365–33.366 ms | ContentFit C3-P |
| M9 | 20 load/unload cycles (`glFinish`, then `glDeleteTextures`) with 0 KiB drift in free VRAM, GL RAM or heap; the 64 MiB VRAM tier then fitted again | ContentFit C4-CYC |
| M10 | 64 MiB of RGBA8 textures resident (19.9 MiB VRAM left); a 96 MiB heap working set; 30 s at 30 fps holding 48 MiB of VRAM and 64 MiB of heap | ContentFit C4 |
| M11 | Uploading 1 MiB RGBA8 textures (512×512) runs at 292–343 MiB/s; a 512×1024 RGBA8 portrait without mips: read, inflate, verify and upload in 0.43 s | ContentFit C4-V, C2-PORT |
| M12 | Redirecting vitaGL's shader cache with link-time wrappers of `sceIoOpen`/`sceIoMkdir` works: 4 folder creations and 10 file opens redirected, 0 refused | ContentFit log |
| M13 | The GDAT Proto reader (`gp_gdat_open`, `gp_wrap_parse`, strict inflate) is correct, including its error paths | GDAT Proto Stage 3 |
| M14 | 128×128 RGBA8 and RGB565 textures with `glGenerateMipmap` display correctly. `texgen` uploads all three textures from one reused buffer, so vitaGL copies data during `glTexImage2D`. `GL_REPEAT`, trilinear and nearest filtering, `GL_MODULATE`, counter-clockwise front faces with back-face culling, and fog all work | Main app, phase 5 |
| M15 | The heap block-size query works (134,217,728 bytes). The ux0 free-space query (`sceIoDevctl`) fails and returns −1 | ContentFit log |
| M16 | ContentFit's CFP1 check (`cf_payload_check`, CRC on) ran on every entry of every pack: on the main thread in the pipelined loads and on the worker during streaming, with 0 errors | ContentFit C2 pipelined, C4-STRM |

### 2.2 Assumptions [A]
| ID | Assumption | Why it is not yet proven | Checked in |
|---|---|---|---|
| A1 | vitaGL copies data during `glCompressedTexImage2D`, so the source buffer can be reused straight away | ContentFit reused one buffer, but its blocks were random, so a wrong copy would not have been visible | 10.8: solid-colour mip levels must look right after their buffer has been reused |
| A2 | DXT1, DXT5, ETC1 and PVRTC4 decode to the intended colours | Same reason | 10.8: you confirm each level's colour |
| A3 | `glBufferData` copies its source (standard GL behaviour), so a vertex or index buffer can be freed after upload | Not tested in isolation | 10.8: the room draws correctly after its source buffers are reused |
| A4 | `GL_NEAREST_MIPMAP_NEAREST` works | Never used | Avoided by the 10.2 fixtures unless tested in 10.8 |
| A5 | Texture sizes and shapes not yet tested work (tested: 128², 256², 512², and 512×1024 without mips) | Not run | 10.8, for the sizes the fixtures use |
| A6 | Checking a resource's CRC-32 and structure on the main thread fits the per-frame budget | zlib's `crc32` speed on the Vita was not measured on its own | 10.8 logs main-thread time per resource |
| A7 | Stopping a load mid-way blocks the main thread for at most about 1.3 s for an 8 MiB entry, or about 0.16 s for a 1 MiB entry. This is computed from M2 and M3, not measured | Not measured | 10.9 measures the stop time |
| A8 | Area loads with inflation on the worker take about as long as ContentFit's pipelined loads (M4) | M4 inflated on the main thread | 10.8 measures |
| A9 | Measured VRAM use of VRS1 textures and buffers is close to the table of contents' byte count, as in M6 | Not run with VRS1 | 10.8 compares |

## 3. Pipeline overview
```
app0:fixtures/<pack>.gdat
 main    gp_gdat_open (strict) ........ archive index                 GDAT Proto, unmodified
 main    gp_gdat_load(entry 0) ........ table of contents (small)     GDAT Proto, unmodified
 main    cf_payload_check(entry 0) .... its CFP1 envelope             ContentFit, unmodified
 main    cf_precheck .................. memory pre-check              ContentFit, unmodified
 worker  cf_ring ...................... sceIoPread -> gp_wrap_parse -> cf_zinflate_exact
                                        -> cf_payload_check (CFP1)    ContentFit, unmodified
 main    rs_format .................... envelope rule, VRS1 checks, policy (section 6)   new, src/resource/
 main    rs_texture / rs_mesh ......... GL upload, then release slot  new, src/resource/
 main    rs_area ...................... resource table, commit, release  new, src/resource/
 main    renderer ..................... camera, player, collision, room_map, hud2d, input, timing (src/, unmodified)
```
- **Reused unmodified:**
  - Phase 8: `rp_io`, `rp_mem`, `rp_inflate`, `rp_time`;
  - GDAT Proto: `gp_common`, `gp_gdat`, `gp_wrap`;
  - ContentFit: `cf_pack` (the ring reader and CFP1 check), `cf_zinflate`, `cf_budget`, `cf_stats`, `cf_common` (section 4.8);
  - the renderer modules listed above.
- **New, in `src/resource/`:** `rs_format`, `rs_loader` (section 6.1 steps A, B through `cf_ring`, C.1–C.3, C.5 and D; it hands each checked resource to its caller for the C.4 upload and makes no GL call itself), `rs_texture`, `rs_mesh`, `rs_area`. The main app's `CMakeLists.txt` does not list them, so VDNG00001 is unaffected.
- **New, app-specific, in `respipe/` (approved, about 190 lines):**
  - the Vita platform functions that `cf_pack` needs (raw reads, threads, semaphores), with ResPipe's read allowlist;
  - the shader-cache redirect to `ux0:data/ResPipe/shader_cache`;
  - the indexed draw by material range.

## 4. Archive and transport
### 4.1 Nesting, from the file to the resource
Each resource is wrapped in four layers. From the outside in:

```
GDAT archive file ................................................ little-endian index
├─ 0      "GDAT", N, then N × {offset, size}
└─ region i, one per entry (size = 0x90 + Z_i bytes)
   ├─ 0x00–0x7F  wrapper header: 128 zero bytes
   ├─ 0x80–0x8F  "BILZ", U_i, Z_i, 7 ............................. big-endian
   └─ 0x90…      zlib stream, Z_i bytes; inflates to exactly U_i bytes:
      ├─ 0–31    CFP1 envelope: "CFP1", kind 2, 0, 0, P_i, CRC, i, 0 ... little-endian
      └─ 32…     VRS1 resource, P_i bytes ........................... little-endian
         ├─ 0–31   VRS1 header: "VRS1", version, kind, id, body_size, body CRC, header CRC
         └─ 32…    body (table of contents, texture, mesh or level)
```

| Quantity | Relationship |
|---|---|
| VRS1 payload size *P*ᵢ | = the table of contents' `payload_size` for entry *i*; 32 ≤ *P*ᵢ ≤ 8,388,576 |
| VRS1 body size | = *P*ᵢ − 32 |
| CFP1 body length | = *P*ᵢ |
| Inflated size *U*ᵢ (the `BILZ` raw size) | = *P*ᵢ + 32 (the CFP1 envelope); *U*ᵢ ≤ 8 MiB |
| Compressed size *Z*ᵢ (the `BILZ` compressed size) | = region size − 0x90; *Z*ᵢ ≤ *U*ᵢ + (*U*ᵢ >> 10) + 64 |
| Region size | = *Z*ᵢ + 144; ≤ 8 MiB |
| Archive file size | = 8 + 8·*N* + Σ region sizes |

| Layer | Read by (thread) | What it checks |
|---|---|---|
| GDAT index | `gp_gdat_open` (main) | magic, count, every entry inside the file, sorted, no gaps, no overlap, nothing after the last entry |
| `BILZ` wrapper | `gp_gdat_load` for entry 0 (main); `gp_wrap_parse` for the rest (worker); ResPipe (main) | magic, sizes, constant 7, compressed size = region − 0x90; ResPipe: header bytes zero, size limits |
| zlib stream | `rp_inflate_exact` for entry 0 (main); `cf_zinflate_exact` for the rest (worker) | complete stream, exact output size, all input used, Adler-32 |
| CFP1 envelope | `cf_payload_check` (main for entry 0, worker for the rest); ResPipe (main) | magic, reserved, index, body length, kind, body CRC; ResPipe: kind must be 2, CRC and size must match the table of contents |
| VRS1 resource | ResPipe `rs_format` (main) | everything in sections 5 and 6 |

### 4.2 Byte order
| Layer | Byte order |
|---|---|
| GDAT archive index | little-endian |
| Entry wrapper (`BILZ` block) | **big-endian** (as GDAT Proto requires) |
| zlib stream | as RFC 1950 defines it (its Adler-32 trailer is big-endian) |
| CFP1 envelope | little-endian |
| VRS1 resources | little-endian; floats are IEEE 754 binary32, little-endian |

### 4.3 Archive (read by `gp_gdat_open`, strict mode)
| Offset | Type | Field |
|---|---|---|
| 0 | char[4] | `"GDAT"` |
| 4 | u32 | entry count *N* |
| 8 | *N* × {u32 offset, u32 size} | one region per entry |

- **Required by GDAT Proto in strict mode:**
  - entry 0 starts at `8 + 8N`;
  - entries are sorted, with no gaps and no overlaps;
  - the last one ends exactly at the end of the file.
- **ResPipe's own limit [D]:** 2 ≤ *N* ≤ 256, otherwise `RS_ERR_LIMIT`. Entry 0 is the table of contents; entries 1 to *N*−1 are resources.

### 4.4 Entry wrapper (the first 0x90 bytes of each region)
| Offset | Type | Field | Rule |
|---|---|---|---|
| 0x00 | 128 bytes | header | **all zero** [D]. GDAT Proto ignores these bytes; ResPipe checks them (`RS_ERR_RESERVED`) |
| 0x80 | char[4] | `"BILZ"` | GDAT Proto |
| 0x84 | u32 BE | raw size *U* | = *P* + 32 (CFP1 envelope + VRS1 payload); at most 8 MiB (8,388,608) [D] |
| 0x88 | u32 BE | compressed size *Z* | = region size − 0x90 (strict); *Z* ≤ *U* + (*U* >> 10) + 64 [D] |
| 0x8C | u32 BE | 7 | GDAT Proto, strict |
| 0x90 | *Z* bytes | zlib stream (RFC 1950) | must inflate to exactly *U* bytes and use every input byte |

The region is limited to 8 MiB as well [D], so that both of a slot's buffers (the region and the inflated bytes) stay within ContentFit's 8 MiB single-allocation limit.

### 4.5 CFP1 envelope [D]
**What it is:**
- The inflated *U* bytes of every entry begin with a 32-byte CFP1 envelope, followed by the VRS1 resource.
- CFP1 is ContentFit's existing payload header (`contentfit/src/cf_pack.h`), used here unchanged.

**Why [D]:**
- ContentFit's background reader (`cf_ring`), with inflation on the worker, runs `cf_payload_check` on every entry. That is the path measured without hitches in M5.
- Wrapping each resource as a CFP1 data entry lets ResPipe reuse that worker unmodified: the worker inflates and checks the CRC of the whole resource.

**Alternatives rejected:**
- A ResPipe-specific worker would duplicate about 150 lines of `cf_pack.c`, beyond the approved app-specific code.
- Changing ContentFit is not allowed.

**Binary layout (32 bytes, little-endian):**

| Offset | Size | Type | Field | Value in ResPipe | Checked by |
|---|---|---|---|---|---|
| 0 | 4 | char[4] | magic | `"CFP1"` (`43 46 50 31`) | `cf_payload_check` |
| 4 | 4 | u32 | kind | 2 (data). ContentFit also defines 1 (texture); ResPipe refuses it | `cf_payload_check` (1 or 2); ResPipe (must be 2) |
| 8 | 4 | u32 | width | 0 | `cf_payload_check` (must be 0 for kind 2) |
| 12 | 4 | u32 | height | 0 | `cf_payload_check` (must be 0 for kind 2) |
| 16 | 4 | u32 | body length | *P* = *U* − 32, the VRS1 payload size; must equal the table of contents' `payload_size` | `cf_payload_check` (= size − 32); ResPipe (= table of contents) |
| 20 | 4 | u32 | body CRC | CRC-32 of the *P* body bytes; must equal the table of contents' `payload_crc` | `cf_payload_check`; ResPipe (= table of contents) |
| 24 | 4 | u32 | index | this entry's GDAT index *i* | `cf_payload_check` |
| 28 | 4 | u32 | reserved | 0 | `cf_payload_check` |

**Version:** CFP1 has no version field. The magic `"CFP1"` names the one and only layout. ContentFit is frozen, so CFP1 cannot change; a different envelope would be a new transport (section 12), never a new CFP1.

**Payload size rules:**
- body length = inflated size − 32 = *P*;
- 32 ≤ *P* ≤ 8,388,576, because VRS1 needs at least its 32-byte header and *U* = *P* + 32 ≤ 8 MiB;
- *P* = the table of contents' `payload_size`.

**Checksum rules:**
- The CRC-32 (section 5.1) covers only the *P* body bytes, not the 32 envelope bytes.
- The envelope header itself is protected by the zlib stream's Adler-32 (which covers all *U* inflated bytes) and by the structural checks above.
- For each entry, the CFP1 body CRC must equal its table-of-contents `payload_crc`. For entry 0, which has no row, the CRC checked when the table of contents was read must equal the CRC the worker sees later.

**Check order:** `cf_payload_check` checks the following, in order. Any failure gives `CF_ERR_PAYLOAD` (211):
1. length ≥ 32 and magic;
2. reserved = 0;
3. index = the entry's GDAT index;
4. body length = length − 32;
5. kind: kind 2 needs width = height = 0. Kind 1 needs power-of-two width and height up to 4096 with 4·width·height = body length. Other kinds are rejected;
6. body CRC.

ResPipe then refuses any kind other than 2 with `RS_ERR_ENVELOPE` (320).

**Naming the failure:** `CF_ERR_PAYLOAD` alone does not say which field failed. So ResPipe re-reads the envelope on the main thread (read-only, in the same order) and writes the failing field to the run log: `magic`, `length`, `reserved`, `index`, `body length`, `kind` or `body CRC`.

### 4.6 Integrity checks at each layer
| Check | Covers | Where |
|---|---|---|
| zlib Adler-32 | all *U* inflated bytes (CFP1 envelope + VRS1 resource) | inflate (worker; main for entry 0) |
| CFP1 body CRC-32 | the *P* VRS1 bytes | `cf_payload_check` (worker; main for entry 0) |
| Table of contents `payload_crc` | the same *P* bytes; must equal the CFP1 body CRC | main |
| VRS1 header CRC | VRS1 bytes 0–27 | main |
| VRS1 body CRC | the VRS1 body | main |

The VRS1 CRCs repeat part of what CFP1 already checks. They are kept so that a VRS1 resource can be checked on its own, without its transport (section 12).

### 4.7 Sizes and overhead
**Per entry:**

| Layer | Bytes | Where it exists |
|---|---|---|
| GDAT index entry | 8 | in the file, and in memory while the pack is open |
| `BILZ` wrapper | 144 | in the file; read into the slot's region buffer |
| CFP1 envelope | 32 | inflated, in the slot's output buffer only; never kept after the slot is released, never uploaded |
| VRS1 header | 32 | inflated; only a level's body is kept after loading |

**In memory:**
- CFP1 adds 32 bytes to every slot's output buffer, and 32 bytes to the table-of-contents buffer.
- It adds nothing to VRAM.
- Section 8.2 counts it in every heap figure.

**The worked pack (section 9):**

| Entry | Resource | *P* (VRS1) | *U* = *P* + 32 (inflated) |
|---|---|---|---|
| 0 | table of contents | 144 | 176 |
| 1 | texture | 152 | 184 |
| 2 | mesh | 188 | 220 |
| 3 | level | 57 | 89 |
| **Total** | | **541** | **669** |

The four CFP1 envelopes are 128 of the 669 inflated bytes. That proportion is only this large because these resources are tiny: for a 64 KiB texture the envelope is 0.05%. The archive is 8 + 32 + 4 × 144 + Σ*Z* = 616 + Σ*Z* bytes. The compressed sizes depend on the zlib build, so they are not listed.

### 4.8 Reusing ContentFit without modifying it
- **Compiled into ResPipe unchanged** (from `contentfit/src/`):
  - `cf_pack.c`, `cf_zinflate.c`, `cf_budget.c`, `cf_stats.c`, `cf_common.c`;
  - included unchanged: `cf_pack.h`, `cf_zinflate.h`, `cf_budget.h`, `cf_stats.h`, `cf_common.h`, `cf_plat.h`.
- **Not compiled into ResPipe:** ContentFit's other 13 source files (`cf_main`, `cf_suite`, `cf_scenes`, `cf_scene`, `cf_geom`, `cf_load`, `cf_io`, `cf_memtest`, `cf_report`, `cf_paths`, `cf_iowrap`, `cf_plat_vita`, `cf_fixinfo`). ContentFit's application, paths, report writer, shader-cache redirect and Vita platform code are therefore not part of ResPipe.
- **Platform functions:** `cf_pack.c` calls ten functions declared in `cf_plat.h`:
  - `cf_raw_open`, `cf_raw_pread`, `cf_raw_close`;
  - `cf_thread_start`, `cf_thread_join`;
  - `cf_sema_create`, `cf_sema_wait`, `cf_sema_try`, `cf_sema_signal`, `cf_sema_delete`.

  ResPipe defines them in its own `respipe/src/rs_plat_vita.c`, part of the approved app-specific code, with its own read allowlist (`app0:fixtures/` only). Defining functions declared in an unchanged header changes nothing in ContentFit. ResPipe does not link `cf_plat_vita.c`, so the names never clash.
- **Checked during stage 10.1 (read-only):**
  - the five files refer to no ContentFit path, title ID or output-folder macro;
  - they include only standard headers, zlib, Phase 8 and GDAT Proto headers, and ContentFit's own headers listed above;
  - `cf_common.h` does define ContentFit's title ID and folder macros. ResPipe includes it but must not use them, and the 10.7 package check will confirm that the ResPipe binary contains neither `ux0:data/ContentFit` nor `VDNG00005`.
- **ContentFit itself (VDNG00005):** its sources, `CMakeLists.txt`, tests, tools, documents and VPK stay exactly as committed (`319ca34`, `e3e3dc4`). `git diff` of `contentfit/` against HEAD is empty, and the same check is part of every later gate.
- **The CFP1 check itself is hardware-tested:** [M] M16.

## 5. VRS1 resource format (version 1)
### 5.1 Conventions
- **Byte order:** little-endian. Floats are IEEE 754 binary32 and must be finite (no NaN, no infinity).
- **Header and body:** a resource is a 32-byte header followed by a body. Every offset inside a body is relative to the start of the body.
- **Exact layout [D]:** every block starts at the position this document defines. Offsets are multiples of 16, every padding byte and reserved field is zero, and the body ends exactly at the end of its last block. One set of contents therefore has exactly one valid encoding.
- **CRC-32:** the zlib / ISO-HDLC CRC (polynomial 0xEDB88320 reflected, initial value and final XOR 0xFFFFFFFF). The check value for `"123456789"` is `CBF43926`. CFP1 uses the same CRC.
- **Alignment in memory:**
  - ResPipe's buffers come from `rp_malloc`, which guarantees 8-byte alignment (`resproto/src/rp_mem.c`). The CFP1 envelope is 32 bytes and every block offset is a multiple of 16, so every block starts 8-byte aligned.
  - Fields are read with byte loads; the format never needs more than 8-byte alignment.
- **Sizes:** "MiB" is 1,048,576 bytes.

### 5.2 Common header (32 bytes)
| Offset | Type | Field | Rule |
|---|---|---|---|
| 0 | char[4] | magic | `"VRS1"` (`56 52 53 31`) |
| 4 | u16 | version | 1 |
| 6 | u16 | kind | 1 = table of contents, 2 = texture, 3 = mesh, 4 = level |
| 8 | u32 | resource_id | 0 for the table of contents; non-zero and unique within the pack otherwise |
| 12 | u32 | body_size | payload size − 32 |
| 16 | u32 | body_crc | CRC-32 of the body |
| 20 | 8 bytes | reserved | zero |
| 28 | u32 | header_crc | CRC-32 of bytes 0–27 |

### 5.3 Kind 1: table of contents
It is always GDAT entry 0, with resource_id 0. It lets ResPipe check memory and cross-check every entry before reading any resource.

**Body:**

| Offset | Type | Field | Rule |
|---|---|---|---|
| 0 | u32 | entry_count | 1–255, = GDAT count − 1 |
| 4 | u32 | total_vram_bytes | = sum of the rows' `vram_bytes` |
| 8 | u32 | total_heap_bytes | = sum of the rows' `heap_bytes` |
| 12 | u32 | reserved | 0 |
| 16 | entry_count × 32 bytes | rows | row *i* describes GDAT entry *i* + 1 |

body_size = 16 + 32 × entry_count, so the payload is 48 + 32 × entry_count bytes and the inflated entry (with CFP1) is 80 + 32 × entry_count bytes: at most 8,240.

**Row:**

| Offset | Type | Field | Rule |
|---|---|---|---|
| 0 | u32 | resource_id | non-zero, unique |
| 4 | u16 | kind | 2, 3 or 4 |
| 6 | u16 | format | texture format code (1–6) for kind 2; 0 otherwise |
| 8 | u32 | payload_size | *P* of that entry (VRS1 only, without the CFP1 envelope): 32 to 8,388,576 |
| 12 | u32 | payload_crc | CRC-32 of that entry's whole VRS1 payload (= its CFP1 body CRC) |
| 16 | u32 | vram_bytes | texture: the sum of its level sizes (with generated mipmaps, the full chain's sizes); mesh: 24 × vertex_count + 2 × index_count; level: 0 |
| 20 | u32 | heap_bytes | level: its body_size (kept in memory after loading); 0 otherwise |
| 24 | 8 bytes | reserved | zero |

### 5.4 Kind 2: texture
**Body header (16 bytes), then the level table:**

| Offset | Type | Field | Rule |
|---|---|---|---|
| 0 | u16 | format | see the format table |
| 2 | u16 | width | power of two, 1–2048 |
| 4 | u16 | height | power of two, 1–2048 |
| 6 | u16 | mip_count | 1, or the full chain: log2(max(width, height)) + 1 |
| 8 | u32 | flags | bit 0 repeat (otherwise clamp to edge); bit 1 linear filtering (otherwise nearest); bit 2 generate mipmaps. Other bits 0 |
| 12 | u32 | reserved | 0 |
| 16 | mip_count × {u32 offset, u32 size} | level table | level 0 is the largest |

**Layout:**
- level 0 starts at `align16(16 + 8 × mip_count)`;
- level *i* + 1 starts at `align16(end of level i)`;
- the body ends exactly at the end of the last level.

Level *i* is `max(1, width >> i)` × `max(1, height >> i)` texels.

| Code | Format | Upload | Bytes in a *w* × *h* level | Status |
|---|---|---|---|---|
| 1 | RGBA8 | `glTexImage2D(GL_RGBA, GL_RGBA, GL_UNSIGNED_BYTE)` | 4·*w*·*h* | [M] M6, M11, M14 |
| 2 | RGB565 | `glTexImage2D(GL_RGB, GL_RGB, GL_UNSIGNED_SHORT_5_6_5)` | 2·*w*·*h* | [M] M6, M14 |
| 3 | DXT1 (RGB) | `glCompressedTexImage2D(GL_COMPRESSED_RGB_S3TC_DXT1_EXT)` | ⌈*w*/4⌉·⌈*h*/4⌉·8 | upload [M] M6; decoding [A] A2 |
| 4 | DXT5 | `glCompressedTexImage2D(GL_COMPRESSED_RGBA_S3TC_DXT5_EXT)` | ⌈*w*/4⌉·⌈*h*/4⌉·16 | upload [M] M6; decoding [A] A2 |
| 5 | ETC1 | `glCompressedTexImage2D(GL_ETC1_RGB8_OES)` | ⌈*w*/4⌉·⌈*h*/4⌉·8 | upload [M] M6; decoding [A] A2 |
| 6 | PVRTC 4bpp | `glCompressedTexImage2D(GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG)` | max(*w*,8)·max(*h*,8)/2 | upload [M] M6; decoding [A] A2 |

**Texture rules:**
- **PVRTC4** must be square [D]: only square textures were tested.
- **RGB565:** any level that is one texel wide must also be one texel high [D]. Every row then ends on a 4-byte boundary, so the layout does not depend on `GL_UNPACK_ALIGNMENT`, which this project has never changed.
- **Generated mipmaps (bit 2):** only for RGBA8 and RGB565 [M] M14, and only with mip_count = 1. Compressed formats must store every level.
- **Texel data:**
  - row 0 is the row sampled at *v* = 0;
  - RGBA8 bytes are R, G, B, A;
  - RGB565 is a little-endian u16 with R in bits 15–11, G in 10–5 and B in 4–0;
  - compressed blocks are stored exactly as each GL extension defines them. VRS1 never reorders or byte-swaps them.
- **Filtering:**
  - linear with mipmaps: `GL_LINEAR_MIPMAP_LINEAR` / `GL_LINEAR` [M];
  - linear without: `GL_LINEAR` / `GL_LINEAR` [M];
  - nearest without mipmaps: `GL_NEAREST` / `GL_NEAREST` [M];
  - nearest with mipmaps: `GL_NEAREST_MIPMAP_NEAREST` / `GL_NEAREST` [A] A4.
- **Wrapping:** `GL_REPEAT` or `GL_CLAMP_TO_EDGE`, both used in ContentFit [M].
- **Alpha:** stored for RGBA8 and DXT5. Phase 10 draws everything opaque [D].
- **Size limit:** the 8 MiB limit on *U* decides the largest texture. RGBA8 1024×1024 with its full chain is 5,592,404 bytes and fits; RGBA8 2048×2048 does not.

### 5.5 Kind 3: mesh
**Body header (32 bytes):**

| Offset | Type | Field | Rule |
|---|---|---|---|
| 0 | u32 | vertex_count | 3–65,536 |
| 4 | u32 | index_count | 3–393,216, a multiple of 3 |
| 8 | u16 | vertex_format | 1 (the layout below) |
| 10 | u16 | vertex_stride | 24 |
| 12 | u16 | range_count | 1–16 |
| 14 | u16 | primitive | 1 = triangle list with u16 indices |
| 16 | u32 | range_offset | = 32 |
| 20 | u32 | vertex_offset | = `align16(32 + 16 × range_count)` |
| 24 | u32 | index_offset | = `align16(vertex_offset + 24 × vertex_count)` |
| 28 | u32 | reserved | 0 |

**Range (16 bytes each):** u32 first_index, u32 index_count, u32 texture_id, u32 reserved (0).
- Ranges are contiguous and in order, starting at index 0 and ending at the last index.
- Each range's count is a positive multiple of 3.
- texture_id is 0 (untextured) or the id of a texture in the same pack.

**Vertex (24 bytes, the renderer's existing `LevelVertex`):**

| Offset | Type | Field |
|---|---|---|
| 0, 4, 8 | f32 | x, y, z |
| 12, 16 | f32 | u, v |
| 20–23 | u8 × 4 | r, g, b, a: vertex colour, multiplied with the texture (`GL_MODULATE`) |

**Indices:** u16, starting at index_offset; each is less than vertex_count. The body ends exactly at `index_offset + 2 × index_count`.

**Conventions [M] M14:** right-handed with +Y up, in world units. Front faces wind counter-clockwise; back faces are culled.

**Upload [M] M7:** one static vertex buffer and one index buffer (`GL_STATIC_DRAW`). Each range is drawn with `glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_SHORT, 2 × first)`.

### 5.6 Kind 4: level
| Offset | Type | Field | Rule |
|---|---|---|---|
| 0 | u16 | width | 1–64 cells |
| 2 | u16 | depth | 1–64 cells |
| 4 | u16 | start_x | less than width; must be an open cell |
| 6 | u16 | start_z | less than depth |
| 8 | f32 | cell_size | finite, greater than 0 |
| 12 | f32 | wall_height | finite, greater than 0 |
| 16 | width × depth bytes | cells | z outer, x inner: 0 = open, 1 = solid |

The body ends exactly after the cells. Any level that meets these rules is a valid VRS1 level, whatever its size or layout. Which levels ResPipe will actually load is a separate application rule (section 5.8).

### 5.7 Limits [D]
| Item | Limit |
|---|---|
| Entries per archive | 2–256 |
| Region (wrapper + compressed stream) | ≤ 8 MiB |
| Inflated entry *U* (CFP1 envelope + VRS1) | ≤ 8 MiB, ContentFit's single-allocation limit |
| VRS1 payload *P* | 32 to 8,388,576 bytes (8 MiB − 32, because of the CFP1 envelope) |
| Compressed size *Z* | ≤ *U* + (*U* >> 10) + 64 |
| Resources per pack | 1–255 |
| Texture | power-of-two sides, 1–2048 |
| Mesh | 3–65,536 vertices; 3–393,216 indices; 1–16 ranges |
| Level | 1–64 cells per side |

### 5.8 ResPipe application policy (not part of the VRS1 format) [D]
These rules come from what the current renderer can do, not from the format. A resource can be valid VRS1 and still be refused by ResPipe Phase 10. A later ResPipe can relax a policy without any change to VRS1.

| Policy | Reason | Error |
|---|---|---|
| Every level must equal the reference room byte for byte | `collision.c` and `room_map.c` stay unmodified and use the hard-coded room; the start position comes from the main app | `RS_ERR_LEVEL_MISMATCH` (319) |

**The reference room:**
- 12 × 12 cells;
- cell_size 2.0 and wall_height 2.5;
- start cell (6, 6), the main app's start;
- the cells of `src/gameplay/room_map.c`;
- a body of 160 bytes (payload 192, inflated 224). It is shown in full in section 9.7.

**Example:**
- **The 3×3 level in section 9.5 is valid VRS1.** It passes every format check in sections 6.2 and 6.3, and the reference checker confirms this.
- **ResPipe Phase 10 refuses it** at the policy step, with `RS_ERR_LEVEL_MISMATCH` (case P-01), only because the current renderer needs the 12×12 room.
- **A future ResPipe with level-driven collision could load the same bytes unchanged.**

## 6. Validation and error handling
There is no lenient mode, and the first failure wins. Every size is checked before anything of that size is allocated.

### 6.1 Order of checks
**A. Opening a pack (main thread)**
1. `gp_gdat_open(path, GP_STRICT)`. On failure: GDAT Proto's own codes. Then ResPipe checks 2 ≤ *N* ≤ 256 (`RS_ERR_LIMIT`).
2. `gp_gdat_load` of entry 0: wrapper, strict inflate. On failure: Phase 8 / GDAT Proto codes.
   - Then the wrapper header must be zero. This is checked from GDAT Proto's record of it: the tag is 0 and the CRC-32 of bytes 0x04–0x7F is `0x15D70E0C`, the value for 124 zero bytes (`RS_ERR_RESERVED`).
3. The CFP1 envelope of entry 0: `cf_payload_check(index 0, CRC on)` (`CF_ERR_PAYLOAD`), then kind = 2 (`RS_ERR_ENVELOPE`).
4. Common header checks (6.2), then the table-of-contents checks (6.3).
5. For every entry *i* ≥ 1, with *U*ᵢ = `payload_size` + 32: the region is at most 8 MiB, and region − 0x90 ≤ *U*ᵢ + (*U*ᵢ >> 10) + 64 (`RS_ERR_LIMIT`).
6. `cf_precheck` with the needs from section 8.2. On failure: `CF_ERR_SKIPPED` or `CF_ERR_UNKNOWN_SIZE`.
7. `cf_ring_start`: *S* slots of *R* + *U* bytes each, with *R* and *U* as defined in section 8.2 (*U* also covers the table of contents, which passes through a slot as entry 0); inflation on the worker. On failure: `RP_ERR_NOMEM`, `RP_ERR_OPEN` or `CF_ERR_THREAD`.

**B. For each entry, on the worker (unmodified ContentFit and GDAT Proto code)**
1. The region size must not exceed the slot buffer (`RP_ERR_LIMIT`).
2. `sceIoPread` must return the whole region (`RP_ERR_SHORT_READ`).
3. `gp_wrap_parse` in strict mode: GDAT Proto codes.
4. The raw size must fit the output buffer (`RP_ERR_LIMIT`).
5. `cf_zinflate_exact`: Phase 8 codes.
6. `cf_payload_check` (section 4.5, with the entry's index and the CRC check on): `CF_ERR_PAYLOAD`.

**C. For each entry, on the main thread, after taking its slot**
1. The slot status, as reported by the worker. For `CF_ERR_PAYLOAD`, the failing CFP1 field is named in the log (section 4.5).
2. **Entry 0:** its CFP1 body CRC must equal the CRC already checked in step A3 (`RS_ERR_TOC`). Then the slot is released.
3. **Other entries:**
   1. wrapper bytes 0x00–0x7F are zero (`RS_ERR_RESERVED`);
   2. CFP1 kind = 2 (`RS_ERR_ENVELOPE`);
   3. inflated size *U* = `payload_size` + 32, and the CFP1 body CRC = `payload_crc` (`RS_ERR_TOC`);
   4. the common header (6.2);
   5. resource_id and kind must equal the table-of-contents row (`RS_ERR_TOC`);
   6. the kind-specific checks (6.3);
   7. the format, and `vram_bytes` and `heap_bytes` recomputed from the resource, must equal the row (`RS_ERR_TOC`). Format is compared here rather than in step 5, so an unknown format reaches the texture's own check and is reported as `RS_ERR_ENUM` (case I-08);
   8. every mesh texture_id must name a texture in the table of contents (`RS_ERR_REF`);
   9. the application policy (5.8): `RS_ERR_LEVEL_MISMATCH`.
4. The GL upload, or for a level, a copy of its body. Then `glGetError` (`CF_ERR_GL`), `cf_guard` for VRAM, GL RAM and heap (`CF_ERR_MARGIN`), and the spill check, where VRAM is the intended pool (`CF_ERR_SPILL`).
5. Release the slot.

**D. After the last entry**
`cf_ring_stop` and `gp_gdat_close`, then **commit** the area (section 7.4). Nothing from a pack is drawn before it is committed.

### 6.2 Common header checks (in order)
1. Payload length ≥ 32 → `RS_ERR_BODY_SIZE`.
2. Magic → `RS_ERR_MAGIC`.
3. version = 1 → `RS_ERR_VERSION`. This comes before the header CRC, so a newer file is reported as a version problem.
4. header_crc → `RS_ERR_HEADER_CRC`.
5. kind 1–4 → `RS_ERR_KIND`.
6. Reserved bytes 20–27 are zero → `RS_ERR_RESERVED`.
7. body_size = length − 32 → `RS_ERR_BODY_SIZE`.
8. body_crc → `RS_ERR_BODY_CRC`.

### 6.3 Kind-specific checks (in order)
**Table of contents:**
1. Kind is 1 and resource_id is 0 → `RS_ERR_TOC`.
2. Body ≥ 16 bytes and = 16 + 32 × entry_count → `RS_ERR_RANGE`.
3. Reserved fields are zero → `RS_ERR_RESERVED`.
4. entry_count is 1–255 and = *N* − 1 → `RS_ERR_TOC`.
5. For each row:
   - the reserved bytes are zero → `RS_ERR_RESERVED`;
   - the id is non-zero and the kind is 2–4 → `RS_ERR_TOC`;
   - the format suits the kind → `RS_ERR_ENUM`;
   - `payload_size` is within limits → `RS_ERR_LIMIT`;
   - the id is unique → `RS_ERR_DUP_ID`.
6. The totals equal the sums → `RS_ERR_TOC`.

**Texture:**
1. Body ≥ 16 → `RS_ERR_RANGE`.
2. Format 1–6 → `RS_ERR_ENUM`.
3. Flags: only bits 0–2, and bit 2 only for formats 1–2 → `RS_ERR_ENUM`.
4. Reserved is zero → `RS_ERR_RESERVED`.
5. Width and height are powers of two up to 2048, and PVRTC4 is square → `RS_ERR_DIMENSION`.
6. mip_count is 1 or the full chain, and bit 2 requires 1 → `RS_ERR_MIP_CHAIN`.
7. The RGB565 one-texel-wide rule → `RS_ERR_DIMENSION`.
8. The level table fits in the body → `RS_ERR_RANGE`.
9. For each level:
   - its offset is a multiple of 16 → `RS_ERR_ALIGN`;
   - its offset is the defined position → `RS_ERR_RANGE`;
   - its size matches the formula → `RS_ERR_LEVEL_SIZE`;
   - it lies inside the body → `RS_ERR_RANGE`;
   - the padding before it is zero → `RS_ERR_RESERVED`.
10. The body ends at the last level → `RS_ERR_RANGE`.

**Mesh:**
1. Body ≥ 32 → `RS_ERR_RANGE`.
2. vertex_format 1, stride 24 and primitive 1 → `RS_ERR_ENUM`.
3. Reserved is zero → `RS_ERR_RESERVED`.
4. The counts are within their limits, and index_count is a multiple of 3 → `RS_ERR_LIMIT`.
5. Block offsets are multiples of 16 → `RS_ERR_ALIGN`.
6. Block offsets are at the defined positions, and the body ends at the last index → `RS_ERR_RANGE`.
7. Padding is zero → `RS_ERR_RESERVED`.
8. For each range: reserved is zero (`RS_ERR_RESERVED`), and it is contiguous with a positive count that is a multiple of 3 (`RS_ERR_RANGE`).
9. The ranges cover every index → `RS_ERR_RANGE`.
10. Every vertex float is finite → `RS_ERR_FLOAT`.
11. Every index is less than vertex_count → `RS_ERR_INDEX`.

**Level:**
1. Body ≥ 16 → `RS_ERR_RANGE`.
2. Width and depth are 1–64 → `RS_ERR_LIMIT`.
3. Body = 16 + width × depth → `RS_ERR_RANGE`.
4. Both sizes are finite and greater than 0 → `RS_ERR_FLOAT`.
5. Every cell is 0 or 1 → `RS_ERR_ENUM`.
6. The start cell is inside the grid and open → `RS_ERR_RANGE`.

### 6.4 Error codes
Codes below 100 are Phase 8 (`rp_err`), 100–199 are GDAT Proto, and 200–299 are ContentFit (`CF_ERR_PAYLOAD` is 211). All of these stay unchanged. ResPipe adds 300–320:

| Code | Name | Meaning |
|---|---|---|
| 300 | `RS_ERR_MAGIC` | not `"VRS1"` |
| 301 | `RS_ERR_VERSION` | version not supported, or different from the table of contents' version |
| 302 | `RS_ERR_KIND` | unknown kind |
| 303 | `RS_ERR_HEADER_CRC` | header CRC wrong |
| 304 | `RS_ERR_BODY_SIZE` | payload shorter than 32 bytes, or body_size disagrees with the length |
| 305 | `RS_ERR_BODY_CRC` | body CRC wrong |
| 306 | `RS_ERR_RESERVED` | a reserved field, padding byte or wrapper header byte is not zero |
| 307 | `RS_ERR_ENUM` | unknown or disallowed value (format, flag bit, vertex format, stride, primitive, cell value) |
| 308 | `RS_ERR_RANGE` | a block or field outside the body, not at its defined position, overlapping, or not covering what it must |
| 309 | `RS_ERR_ALIGN` | an offset that is not a multiple of 16 |
| 310 | `RS_ERR_DIMENSION` | a texture size not allowed |
| 311 | `RS_ERR_MIP_CHAIN` | a mip count not allowed |
| 312 | `RS_ERR_LEVEL_SIZE` | a level size that does not match its formula |
| 313 | `RS_ERR_INDEX` | an index ≥ vertex_count |
| 314 | `RS_ERR_FLOAT` | a float that is NaN or infinite, or a level size ≤ 0 |
| 315 | `RS_ERR_TOC` | the table of contents disagrees with the archive, an envelope or a resource |
| 316 | `RS_ERR_DUP_ID` | resource id used twice |
| 317 | `RS_ERR_REF` | a mesh names a texture that is not in the pack |
| 318 | `RS_ERR_LIMIT` | a count or size above the limits in 5.7 |
| 319 | `RS_ERR_LEVEL_MISMATCH` | **application policy, not a format error:** a valid level that is not the reference room |
| 320 | `RS_ERR_ENVELOPE` | a CFP1 envelope that passes ContentFit's check but is not kind 2 |

### 6.5 What each layer does with a bad header, a wrong size or a failed checksum
| Layer | Bad header | Wrong size | Checksum failure | Detected by |
|---|---|---|---|---|
| GDAT index | magic → `RP_ERR_BAD_MAGIC` (11); more than 65,536 entries → `RP_ERR_COUNT` (16); 0 entries → `GP_ERR_EMPTY` (105); 1 or more than 256 entries → `RS_ERR_LIMIT` (318) | index past the end of the file → `RP_ERR_TRUNCATED` (9); entry outside the file → `RP_ERR_RANGE` (17); entry inside the index → `GP_ERR_IN_INDEX` (101); unsorted → `GP_ERR_UNSORTED` (102); gap → `GP_ERR_GAP` (103); bytes after the last entry → `GP_ERR_TRAILING` (104); overlap → `RP_ERR_OVERLAP` (18) | none at this layer | `gp_gdat_open`, main |
| Reading a region | — | larger than the slot buffer → `RP_ERR_LIMIT` (19); short read → `RP_ERR_SHORT_READ` (5) | — | `cf_ring` worker |
| `BILZ` wrapper | magic → `RP_ERR_BAD_MAGIC` (11); constant not 7 → `GP_ERR_CONST` (107); header bytes not zero → `RS_ERR_RESERVED` (306) | region under 0x90 → `RP_ERR_TRUNCATED` (9); compressed size larger than the region → `RP_ERR_RANGE` (17), smaller → `GP_ERR_SIZE_RULE` (106); raw size over 16 MiB → `RP_ERR_LIMIT` (19); ResPipe's limits (5.7) → `RS_ERR_LIMIT` (318) | — | `gp_wrap_parse` (worker; main for entry 0); ResPipe, main |
| zlib stream | — | stream ends early → `RP_ERR_TRUNCATED` (9); output longer or shorter than the raw size, or input left over → `RP_ERR_SIZE_MISMATCH` (10) | Adler-32 or corrupt data → `RP_ERR_ZLIB_DATA` (21) | `cf_zinflate_exact` (worker); `rp_inflate_exact` (main, entry 0) |
| CFP1 envelope | magic, reserved, index, kind → `CF_ERR_PAYLOAD` (211); kind other than 2 → `RS_ERR_ENVELOPE` (320) | under 32 bytes or body length ≠ size − 32 → `CF_ERR_PAYLOAD` (211); size ≠ table of contents → `RS_ERR_TOC` (315) | body CRC → `CF_ERR_PAYLOAD` (211); CRC ≠ table of contents → `RS_ERR_TOC` (315) | `cf_payload_check` (worker; main for entry 0); ResPipe, main. The failing field is named in the log |
| VRS1 header | magic (300), version (301), kind (302), reserved (306) | body_size (304) | header CRC (303), body CRC (305) | ResPipe, main |
| VRS1 body | values (307), reserved and padding (306) | range (308), alignment (309), dimensions (310), mip chain (311), level size (312), index (313), limits (318), floats (314) | covered by the body CRC | ResPipe, main |
| Table of contents | — | counts, sizes, totals, id and kind → `RS_ERR_TOC` (315); duplicate id → 316; missing texture → 317 | `payload_crc` ≠ envelope → `RS_ERR_TOC` (315) | ResPipe, main |
| Application policy | — | level is not the reference room → `RS_ERR_LEVEL_MISMATCH` (319) | — | ResPipe, main |

### 6.6 Handling a failure
- **The whole load fails:**
  - the first error ends the load;
  - everything acquired for it is released (section 7.5);
  - no resource from that pack is ever drawn;
  - nothing is retried.
- **The active area is untouched:** what was on screen stays there. If there was none, ResPipe shows its built-in error screen.
- **Recorded:** the run log records:
  - the pack, GDAT entry, resource id and layer;
  - the check, with the CFP1 field for `CF_ERR_PAYLOAD`;
  - the byte offset, where there is one;
  - the code's name and number.

  The HUD shows the code name.

## 7. Ownership, threads and cleanup
### 7.1 Ownership
| Object | Created by | Owner | Released by (main thread) | When |
|---|---|---|---|---|
| GDAT index (`gp_gdat`) | `gp_gdat_open` | pending area | `gp_gdat_close` | after `cf_ring_stop`, or on failure |
| Table of contents buffer (CFP1 envelope + VRS1, *Ut* bytes) | `gp_gdat_load` | pending area, then active area | `rp_free` | when the area is released |
| Ring slots (region + inflated bytes, including the CFP1 envelope), zlib arena, worker file, semaphores, worker thread | `cf_ring_start` | the ring | `cf_ring_stop` (safe to call twice) | at the end of the load, or on failure |
| Textures and buffers | main thread (`glGen*`), recorded straight away | pending area, then active area | `glFinish`, then `glDelete*` | on failure, or when the area is replaced |
| Level body copy | main thread (`rp_malloc`) | pending area, then active area | `rp_free` | when the area is released |
| Resource table | main thread (`rp_malloc`) | pending area, then active area | `rp_free` | when the area is released |

**The envelope is never kept:** the CFP1 envelope exists only inside a slot. Texture levels and mesh data are uploaded straight from the slot's buffer, behind the envelope and the VRS1 header, and the slot is released after the upload. This relies on M14 for RGBA8 and RGB565, and on A1 and A3 for the others.

### 7.2 Worker thread rules (unmodified `cf_ring`) [M] M5
**Allowed:**
- `sceIoPread` on its own file;
- `gp_wrap_parse` (no allocation);
- `cf_zinflate_exact` into the slot, using the ring's arena;
- `cf_payload_check`;
- semaphores.

**Never:**
- `rp_malloc`, `malloc` or `free`. The Phase 8 allocator's counters are not thread-safe, and zlib's default allocator calls `malloc`.
- stdio;
- any GL or SDL call;
- logging;
- VRS1 checks or the ResPipe envelope rule. Those run on the main thread.

### 7.3 Main thread (graphics) rules
- **Every GL call happens on the main thread:**
  - every texture and buffer is created there;
  - `glGetError` is cleared before each resource and checked after it.
- **Loading screen [D]:** ready entries are processed until the main thread has spent 20 ms on load work in the current frame. The rest of the 33.3 ms frame draws the loading screen.
- **Streaming [D]:** at most one entry per frame. Basis: M5's slowest upload was 6.66 ms with no hitch.
- **Deleting textures and buffers:** only after `glFinish` [D], as the main app does on its RGB565 toggle and ContentFit did in M9.
- **Slots:** 3 slots for loading-screen loads and 2 for streaming [D], the configurations measured in M4 and M5.

### 7.4 Area states
- **The sequence:** `idle` → `loading` (the pending area) → `committed` (the active area) → `releasing`.
- **Commit:** the pending area becomes active only when every entry has passed every check and been uploaded.
- **Streaming:** while it runs, the active area keeps drawing and the pending area loads beside it. Both count against memory: the pre-check uses the free memory measured at that moment, which already includes the active area.
- **Replacing an area:** after a commit, the previous area is released with `glFinish` first, so no frame that used it is still in flight.

### 7.5 Release (used for every exit: success, failure, cancel, exit)
1. `cf_ring_stop`. It sets the stop flag, wakes the worker and waits for it, closes its file, and frees the slots and arena, together with any CFP1 envelopes in them.
   - A worker in the middle of a read or inflation finishes that entry first, so the main thread waits for at most one entry. That is about 1.3 s for an 8 MiB entry, or about 0.16 s for a 1 MiB entry [A] A7. Streaming packs therefore keep entries at or below 1 MiB [D].
2. `gp_gdat_close`.
3. `glFinish`, then delete every texture and buffer recorded for the area.
4. `rp_free` every recorded buffer (the table of contents, level copies, resource table), setting each pointer to NULL.
5. Verify, compared with the values before the load:
   - tracked `rp_malloc` bytes are back exactly;
   - every vitaGL pool and the heap are back within 64 KiB (`cf_baseline_ok`);
   - the number of live GL objects is back.

   If not: `CF_ERR_BASELINE` is logged and no further load is started until ResPipe restarts [D].

| Failure point | Detected by | Released |
|---|---|---|
| Opening the archive, entry 0's wrapper, inflation or CFP1 envelope, or the table of contents | main, steps A1–A5 | the index and table-of-contents buffer (no ring yet) |
| Memory pre-check | main, A6 | the same; nothing else was allocated |
| Starting the ring | `cf_ring_start` | it stops itself; then the same as above |
| An entry fails on the worker (read, wrapper, inflation, CFP1) | main, C1 | everything (section 7.5) |
| An entry fails the envelope rule, a VRS1 check or the policy | main, C3 | everything |
| GL error, margin or spill | main, C4 | everything, including that entry's GL names, which are recorded before upload |
| START, app exit, or a new area requested while streaming | main | everything belonging to the pending area; the active area is untouched |

### 7.6 Interrupted loads
A load can be stopped by START, by app exit, by a new area request while streaming, or by an error. Every case ends with the release in 7.5:

| Where the load is | What happens |
|---|---|
| Opening the pack (A1–A7, main thread, synchronous) | the current step finishes; the stop is handled straight after |
| The worker waiting for a free slot | `cf_ring_stop` wakes it; it exits without reading |
| The worker reading a region | the read finishes; the result is thrown away |
| The worker inflating or checking a CFP1 envelope | it finishes that entry; the result is thrown away |
| Slots filled but not yet taken | freed unread by `cf_ring_stop` |
| The main thread checking a resource | stops are handled between entries, so the current check finishes; the result is thrown away |
| The main thread uploading | the GL call finishes; its names were recorded first, so they are deleted after `glFinish` |
| After commit | nothing is loading; the area is active and is released only when it is replaced or the app exits |

Nothing from an interrupted load is ever drawn, and no CFP1 envelope, slot or GL object outlives the release.

## 8. Memory budget
### 8.1 Pools and margins
- **Pool sizes:** measured [M] M1.
- **Margins [D], carried over from ContentFit's approved Stage 4 plan:** VRAM 16 MiB, vitaGL RAM 16 MiB, heap 24 MiB, kernel 8 MiB. A single allocation is at most 8 MiB.
- **The pre-check:** `cf_precheck` runs a step only if `free ≥ margin + need + ⌈need / 100⌉` for every pool it needs. A pool whose size cannot be measured is never used.

### 8.2 What a pack needs (the CFP1 envelope included)
- *N* = entry count;
- *R* = the largest region size (144 + *Z*);
- *U* = the largest inflated size of any entry, the table of contents included: max(largest `payload_size`, 48 + 32 × entry_count) + 32. Both terms are VRS1 payload sizes (header and body); the 32 is the one CFP1 envelope each inflated entry carries. So *U* ≥ *Ut*, and *U* ≤ 8 MiB;
- *S* = the number of slots;
- *Rt*, *Ut* = the table of contents' region and inflated size (*Ut* = 80 + 32 × entry_count).

| Pool | Need |
|---|---|
| VRAM | `total_vram_bytes` [M] M6: textures and buffers land in VRAM. The CFP1 envelope is never uploaded |
| Heap | 16·*N* (index) + *Rt* + *Ut* (table of contents, read with `gp_gdat_load`) + *S*·(*R* + *U*) (slots) + 65,536 (arena) + 64·*N* (resource table) + `total_heap_bytes` |
| Kernel | 131,072 (the worker's stack) |
| vitaGL RAM | 0 (the loader allocates none). Guarded after every upload, not pre-checked |

**CFP1's share:** 32 bytes in *Ut* and 32 bytes in each slot's output buffer: 32·(*S* + 1) bytes in all.

**Worked example pack (section 9):**
- **VRAM:** 164 bytes.
- **Heap:** 66,057 bytes plus *Rt* + *S*·(*R* + 220). The 66,057 is the index 64, table of contents *Ut* = 176, arena 65,536, resource table 256 and level 25.
- **CFP1's share:** *Ut* = 176 and *U* = 220 each include a 32-byte envelope, so CFP1 adds 128 bytes with 3 slots.

The real Phase 10 packs (stage 10.2) will be well under the measured free memory:
- the planned largest is an 8 MiB area;
- ContentFit loaded 8–64 MiB packs, with the same envelope, within the same margins (M4, M10).

### 8.3 During and after a load
- **After every upload:** `cf_guard` for VRAM, vitaGL RAM and heap, and the spill check with VRAM as the intended pool.
- **After release:** the baseline check in section 7.5.

## 9. Worked examples: a valid pack
A four-entry pack:
- entry 0: the table of contents;
- entry 1: a texture (id 1);
- entry 2: a mesh (id 2);
- entry 3: a level (id 3).

The sizes at each layer are in section 4.7. Every payload passes every format check, and every entry passes every transport layer. The reference checker confirms both.

### 9.1 Table of contents (144 bytes; inflated entry with CFP1: 176)
- **Header:** kind 1, id 0, body_size 112, body_crc `601C48AE`, header_crc `D6983C7A`.
- **Body:** entry_count 3, total_vram_bytes 164, total_heap_bytes 25.
- **Rows:**

| Row | id | kind | format | payload_size | payload_crc | vram_bytes | heap_bytes |
|---|---|---|---|---|---|---|---|
| 0 | 1 | 2 | 3 | 152 | `9953FCD2` | 56 | 0 |
| 1 | 2 | 3 | 0 | 188 | `C4635B26` | 108 | 0 |
| 2 | 3 | 4 | 0 | 57 | `3863F641` | 0 | 25 |

<!-- hex:toc -->
```text
0000  56 52 53 31 01 00 01 00  00 00 00 00 70 00 00 00
0010  AE 48 1C 60 00 00 00 00  00 00 00 00 7A 3C 98 D6
0020  03 00 00 00 A4 00 00 00  19 00 00 00 00 00 00 00
0030  01 00 00 00 02 00 03 00  98 00 00 00 D2 FC 53 99
0040  38 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
0050  02 00 00 00 03 00 00 00  BC 00 00 00 26 5B 63 C4
0060  6C 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
0070  03 00 00 00 04 00 00 00  39 00 00 00 41 F6 63 38
0080  00 00 00 00 19 00 00 00  00 00 00 00 00 00 00 00
```

### 9.2 Texture: DXT1, 8×8, full mip chain (152 bytes; inflated 184)
- **Header:** kind 2, id 1, body_size 120, body_crc `B5D988F9`, header_crc `D0D86B53`.
- **Body:** format 3, 8×8, mip_count 4, flags 3 (repeat and linear).
- **Level table (body offsets):**
  - level 0: offset 48, size 32;
  - level 1: offset 80, size 8;
  - level 2: offset 96, size 8;
  - level 3: offset 112, size 8.

  So level 0 starts at `align16(16 + 32)` = 48, and the body ends at 120.
- **Content:** each level is solid colour, using one DXT1 block per 4×4 texels with color0 = color1 and all indices 0: red, then green, blue and white. A wrong level or a bad decode would show up immediately on screen (A2).
- **Padding:** payload offsets 0x58–0x5F, 0x78–0x7F and 0x88–0x8F are padding.

<!-- hex:texture -->
```text
0000  56 52 53 31 01 00 02 00  01 00 00 00 78 00 00 00
0010  F9 88 D9 B5 00 00 00 00  00 00 00 00 53 6B D8 D0
0020  03 00 08 00 08 00 04 00  03 00 00 00 00 00 00 00
0030  30 00 00 00 20 00 00 00  50 00 00 00 08 00 00 00
0040  60 00 00 00 08 00 00 00  70 00 00 00 08 00 00 00
0050  00 F8 00 F8 00 00 00 00  00 F8 00 F8 00 00 00 00
0060  00 F8 00 F8 00 00 00 00  00 F8 00 F8 00 00 00 00
0070  E0 07 E0 07 00 00 00 00  00 00 00 00 00 00 00 00
0080  1F 00 1F 00 00 00 00 00  00 00 00 00 00 00 00 00
0090  FF FF FF FF 00 00 00 00
```

### 9.3 Mesh: one floor quad (188 bytes; inflated 220)
- **Header:** kind 3, id 2, body_size 156, body_crc `EAB2FEE9`, header_crc `47B76AC1`.
- **Body header:** 4 vertices, 6 indices, vertex_format 1, stride 24, 1 range, primitive 1; offsets 32 / 48 / 144.
- **Range:** first 0, count 6, texture 1.
- **Vertices:** the floor of cell (0, 0), with the same positions, UVs and winding as `level_mesh.c`'s floor. The corners are (0,0,0), (0,0,2), (2,0,2) and (2,0,0), with UVs (0,0), (0,1), (1,1) and (1,0); the colour is 235, 235, 235, 255.
- **Indices:** 0 1 2 0 2 3. The face points up (+Y).

<!-- hex:mesh -->
```text
0000  56 52 53 31 01 00 03 00  02 00 00 00 9C 00 00 00
0010  E9 FE B2 EA 00 00 00 00  00 00 00 00 C1 6A B7 47
0020  04 00 00 00 06 00 00 00  01 00 18 00 01 00 01 00
0030  20 00 00 00 30 00 00 00  90 00 00 00 00 00 00 00
0040  00 00 00 00 06 00 00 00  01 00 00 00 00 00 00 00
0050  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
0060  00 00 00 00 EB EB EB FF  00 00 00 00 00 00 00 00
0070  00 00 00 40 00 00 00 00  00 00 80 3F EB EB EB FF
0080  00 00 00 40 00 00 00 00  00 00 00 40 00 00 80 3F
0090  00 00 80 3F EB EB EB FF  00 00 00 40 00 00 00 00
00A0  00 00 00 00 00 00 80 3F  00 00 00 00 EB EB EB FF
00B0  00 00 01 00 02 00 00 00  02 00 03 00
```

### 9.4 CFP1 envelopes
**Entry 0** (the table of contents; the first 32 of its 176 inflated bytes): kind 2, body length 144, body CRC `947D28A1`, index 0.

<!-- hex:envelope0 -->
```text
0000  43 46 50 31 02 00 00 00  00 00 00 00 00 00 00 00
0010  90 00 00 00 A1 28 7D 94  00 00 00 00 00 00 00 00
```

**Entry 1** (the texture; the first 32 of its 184 inflated bytes): kind 2, body length 152, body CRC `9953FCD2` (the same as row 0's `payload_crc`), index 1.

<!-- hex:envelope1 -->
```text
0000  43 46 50 31 02 00 00 00  00 00 00 00 00 00 00 00
0010  98 00 00 00 D2 FC 53 99  01 00 00 00 00 00 00 00
```

**Entry 3, complete** (all 89 inflated bytes): the CFP1 envelope (kind 2, body length 57, body CRC `3863F641` = row 2's `payload_crc`, index 3), followed by the 57-byte level of 9.5.

<!-- hex:entry3 -->
```text
0000  43 46 50 31 02 00 00 00  00 00 00 00 00 00 00 00
0010  39 00 00 00 41 F6 63 38  03 00 00 00 00 00 00 00
0020  56 52 53 31 01 00 04 00  03 00 00 00 19 00 00 00
0030  E8 3D F8 DA 00 00 00 00  00 00 00 00 80 0E EA 65
0040  03 00 03 00 01 00 01 00  00 00 00 40 00 00 20 40
0050  01 01 01 01 00 01 01 01  01
```

### 9.5 Level: 3×3 (57 bytes; inflated 89). Valid VRS1, refused by ResPipe's policy
- **Header:** kind 4, id 3, body_size 25, body_crc `DAF83DE8`, header_crc `65EA0E80`.
- **Body:** 3 × 3 cells, start (1, 1), cell_size 2.0, wall_height 2.5. Every cell is solid except the centre.
- **Format:** valid VRS1. It passes every check in sections 6.2 and 6.3.
- **ResPipe Phase 10:** refused at the policy step (5.8) with `RS_ERR_LEVEL_MISMATCH`, because it is not the 12×12 reference room (case P-01).

<!-- hex:level -->
```text
0000  56 52 53 31 01 00 04 00  03 00 00 00 19 00 00 00
0010  E8 3D F8 DA 00 00 00 00  00 00 00 00 80 0E EA 65
0020  03 00 03 00 01 00 01 00  00 00 00 40 00 00 20 40
0030  01 01 01 01 00 01 01 01  01
```

### 9.6 Archive layout of the example pack
- *N* = 4. The index occupies bytes 8–39, and entry 0 starts at offset 40.
- Each region is the 144-byte wrapper followed by the zlib stream of its inflated entry: 176, 184, 220 and 89 bytes, each CFP1 envelope + VRS1.

### 9.7 Level: the reference room (192 bytes; inflated 224). Valid VRS1, accepted by ResPipe's policy
- **Header:** kind 4, id 3, body_size 160, body_crc `C73973BC`, header_crc `64C53B6A`.
- **Body:** 12 × 12 cells, start (6, 6), cell_size 2.0, wall_height 2.5, and the 144 cells of `room_map.c`. Row 0 is all walls; row 6 is `#.......#..#`.

<!-- hex:room -->
```text
0000  56 52 53 31 01 00 04 00  03 00 00 00 A0 00 00 00
0010  BC 73 39 C7 00 00 00 00  00 00 00 00 6A 3B C5 64
0020  0C 00 0C 00 06 00 06 00  00 00 00 40 00 00 20 40
0030  01 01 01 01 01 01 01 01  01 01 01 01 01 00 00 00
0040  00 00 00 00 00 00 00 01  01 00 00 00 00 00 00 00
0050  00 00 00 01 01 00 00 00  01 01 00 00 00 00 00 01
0060  01 00 00 00 01 01 00 00  00 00 00 01 01 00 00 00
0070  00 00 00 00 00 00 00 01  01 00 00 00 00 00 00 00
0080  01 00 00 01 01 00 00 00  00 00 00 00 00 00 00 01
0090  01 00 00 00 00 00 00 00  00 00 00 01 01 00 00 01
00A0  00 00 00 00 00 00 00 01  01 00 00 00 00 00 00 00
00B0  00 00 00 01 01 01 01 01  01 01 01 01 01 01 01 01
```

## 10. Invalid examples
Each case starts from the valid pack in section 9 and makes one change. Each is sent through every layer: wrapper, inflation, CFP1, VRS1 and policy.
- **"CRCs updated":** the VRS1 body and header CRCs are recomputed after the change, and so are the CFP1 CRC and the table-of-contents row, so that the targeted check is reached.
- **"No CRC update":** they are left as they were.

These cases are the test list for stages 10.2–10.4. During stage 10.1, every row was checked with an independent reference implementation of these rules (including its own CFP1 check, written from section 4.5 rather than taken from ContentFit), and it reported the code shown.

### 10.1 VRS1 format
| ID | Starts from | Change | First failing check | Error (code) |
|---|---|---|---|---|
| I-01 | texture | bytes 0–3 = `VRS2` | magic | `RS_ERR_MAGIC` (300) |
| I-02 | texture | version = 2, header CRC updated | version | `RS_ERR_VERSION` (301) |
| I-03 | texture | kind = 9, header CRC updated | kind | `RS_ERR_KIND` (302) |
| I-04 | texture | header_crc + 1 | header CRC | `RS_ERR_HEADER_CRC` (303) |
| I-05 | texture | body_size = 121, header CRC updated | body size | `RS_ERR_BODY_SIZE` (304) |
| I-06 | texture | payload byte 0x50 = 0x01, no VRS1 CRC update | body CRC | `RS_ERR_BODY_CRC` (305) |
| I-07 | texture | header byte 20 = 1, header CRC updated | reserved header bytes | `RS_ERR_RESERVED` (306) |
| I-08 | texture | format = 7, CRCs updated | format | `RS_ERR_ENUM` (307) |
| I-09 | texture | flags = 7 (generate mipmaps on DXT1), CRCs updated | flags | `RS_ERR_ENUM` (307) |
| I-10 | texture | width = 6, CRCs updated | dimensions | `RS_ERR_DIMENSION` (310) |
| I-11 | texture | mip_count = 3, CRCs updated | mip chain | `RS_ERR_MIP_CHAIN` (311) |
| I-12 | texture | level 1 offset = 84, CRCs updated | level alignment | `RS_ERR_ALIGN` (309) |
| I-13 | texture | level 1 size = 16, CRCs updated | level size | `RS_ERR_LEVEL_SIZE` (312) |
| I-14 | texture | body byte 88 (padding) = 0xFF, CRCs updated | padding | `RS_ERR_RESERVED` (306) |
| I-15 | mesh | last index = 4, CRCs updated | index range | `RS_ERR_INDEX` (313) |
| I-16 | mesh | vertex 0 x = NaN, CRCs updated | finite floats | `RS_ERR_FLOAT` (314) |
| I-17 | mesh | range index_count = 3, CRCs updated | ranges cover the indices | `RS_ERR_RANGE` (308) |
| I-18 | mesh | vertex_stride = 20, CRCs updated | vertex format | `RS_ERR_ENUM` (307) |
| I-19 | mesh | range texture_id = 7, CRCs updated | reference to the table of contents | `RS_ERR_REF` (317) |
| I-20 | level | centre cell = 2, CRCs updated | cell values | `RS_ERR_ENUM` (307) |
| I-21 | level | start_x = 0 (a solid cell), CRCs updated | start cell | `RS_ERR_RANGE` (308) |
| I-22 | table of contents | archive has 5 entries, table of contents lists 3 | entry count | `RS_ERR_TOC` (315) |
| I-23 | table of contents | row 0 payload_size = 153, CRCs updated | entry vs table of contents | `RS_ERR_TOC` (315) |
| I-24 | table of contents | row 1 resource_id = 1, CRCs updated | unique ids | `RS_ERR_DUP_ID` (316) |
| I-25 | table of contents | total_vram_bytes = 165, CRCs updated | totals | `RS_ERR_TOC` (315) |
| I-26 | table of contents | row 0 payload_size = 8,388,577, CRCs updated | size limit | `RS_ERR_LIMIT` (318) |

### 10.2 ResPipe application policy (the resource is valid VRS1)
| ID | Starts from | Change | First failing check | Error (code) |
|---|---|---|---|---|
| P-01 | level | none: the valid 3×3 level of 9.5 | reference room (policy, 5.8) | `RS_ERR_LEVEL_MISMATCH` (319) |

### 10.3 CFP1 envelope
All changes are to the envelope of entry 1 (the texture) unless stated.

| ID | Starts from | Change | First failing check | Error (code) |
|---|---|---|---|---|
| C-01 | entry 1 | magic = `CFP2` | CFP1 magic | `CF_ERR_PAYLOAD` (211) |
| C-02 | entry 1 | inflated entry cut to 31 bytes (raw size 31) | CFP1 length (under 32 bytes) | `CF_ERR_PAYLOAD` (211) |
| C-03 | entry 1 | reserved (offset 28) = 1 | CFP1 reserved | `CF_ERR_PAYLOAD` (211) |
| C-04 | entry 1 | index = 2 | CFP1 index | `CF_ERR_PAYLOAD` (211) |
| C-05 | entry 1 | body length = 153 (one more than the payload) | CFP1 body length | `CF_ERR_PAYLOAD` (211) |
| C-06 | entry 1 | body length = 151 (one less) | CFP1 body length | `CF_ERR_PAYLOAD` (211) |
| C-07 | entry 1 | kind = 3 | CFP1 kind | `CF_ERR_PAYLOAD` (211) |
| C-08 | entry 1 | width = 1 on a kind 2 envelope | CFP1 kind (data entries need width and height 0) | `CF_ERR_PAYLOAD` (211) |
| C-09 | entry 1 | body CRC + 1 | CFP1 body CRC | `CF_ERR_PAYLOAD` (211) |
| C-10 | entry 1 | payload byte 0x50 = 0x01, no CRC update | CFP1 body CRC (found before VRS1's own body CRC) | `CF_ERR_PAYLOAD` (211) |
| C-11 | a 4×4 level (64 bytes) as entry 1 | kind = 1, width = height = 4: every ContentFit rule passes | ResPipe envelope rule (kind must be 2) | `RS_ERR_ENVELOPE` (320) |
| C-12 | entry 1 | a different valid texture (level 0 green instead of red), envelope and VRS1 CRCs updated, table of contents unchanged | envelope CRC vs table of contents | `RS_ERR_TOC` (315) |

### 10.4 Wrapper and archive
| ID | Starts from | Change | First failing check | Error (code) |
|---|---|---|---|---|
| W-01 | entry 1 | wrapper byte 0x10 = 1 | wrapper header (main) | `RS_ERR_RESERVED` (306) |
| W-02 | entry 1 | `BILZ` constant = 6 | GDAT Proto, strict (worker) | `GP_ERR_CONST` (107) |
| W-03 | entry 1 | compressed size = region − 0x91 | GDAT Proto size rule (worker) | `GP_ERR_SIZE_RULE` (106) |
| W-04 | entry 1 | one byte in the middle of the zlib stream inverted | inflate (worker) | `RP_ERR_ZLIB_DATA` (21) |
| W-05 | entry 1 | magic = `BILY` | wrapper magic (worker) | `RP_ERR_BAD_MAGIC` (11) |
| W-06 | entry 1 | raw size = 185 (one more than the stream gives) | inflate size (worker) | `RP_ERR_SIZE_MISMATCH` (10) |
| W-07 | entry 1 | raw size = 183 (one less) | inflate size (worker) | `RP_ERR_SIZE_MISMATCH` (10) |
| W-08 | entry 1 | region of 8,388,609 bytes in the GDAT index | region limit (main, A5) | `RS_ERR_LIMIT` (318) |
| W-09 | entry 1 | compressed size 249 for *U* = 184 (limit 248) | compressed-size limit (main, A5) | `RS_ERR_LIMIT` (318) |
| W-10 | archive | 1 entry (the table of contents only) | entry count (main, A1) | `RS_ERR_LIMIT` (318) |
| W-11 | archive | 257 entries | entry count (main, A1) | `RS_ERR_LIMIT` (318) |

**On W-04:** where a damaged stream fails depends on where the damage falls. Other positions can instead give `RP_ERR_TRUNCATED` or `RP_ERR_SIZE_MISMATCH`, and the fixtures in 10.2 will use positions whose result is known.

## 11. Performance targets (stable 30 fps)
| Target [D] | Basis |
|---|---|
| Stable 30 fps by ContentFit's rules: at most 1% of intervals over 34.0 ms, P95 ≤ 34.0 ms, none over 51.0 ms; swap interval 2 | M8; 30 fps was chosen as the target |
| The room pack loads in ≤ 1 s behind the loading screen | M4 (8 MiB in 0.63–0.64 s), A8 |
| An 8 MiB area loads in ≤ 2 s | ContentFit's Stage 4 target; M4 |
| While streaming: no interval over 34 ms, and memory back to its baseline afterwards | M5, M9 |
| Main-thread time per resource is logged; a resource over 8 ms is reported | A6 |

## 12. Versioning and compatibility
- **Identity:** the magic `"VRS1"` plus the u16 version. This document defines version 1.
- **Freezing:** once approved, version 1 never changes meaning. Any change to bytes, rules, codes or limits that a valid version 1 resource depends on defines version 2, with its own approval.
- **Stable prefix:** bytes 0–7 (magic, version, kind) keep their position and meaning in every future version, so any reader can identify a resource and reject it cleanly.
- **Strict readers:**
  - a reader accepts only versions it implements; a version 1 reader rejects anything else with `RS_ERR_VERSION` and never reads part of a resource;
  - every resource in a pack has the table of contents' version.
- **Room for extension:** reserved fields, padding and unused flag bits must be zero, and readers check this. A later version can therefore use them without a version 1 reader misreading the data; the reader fails cleanly instead.
- **New values:** new kinds, texture formats, vertex formats or primitives require a new version. A version 1 reader reports them as `RS_ERR_KIND` or `RS_ERR_ENUM`.
- **The transport is separate:**
  - the container (GDAT, `BILZ` and the CFP1 envelope) is described in section 4 and can be replaced without changing the VRS1 version, because VRS1 resources carry their own sizes and CRCs;
  - CFP1 itself has no version field and cannot change, because ContentFit is frozen. Replacing it would be a transport change, documented and approved separately.
- **Application policy is separate:** a policy such as 5.8 belongs to a ResPipe version, not to VRS1. Relaxing it needs no format change.
- **Tools:** the 10.2 generator writes, and its checker accepts, exactly version 1 inside CFP1 envelopes. The checker must reproduce every case in section 10.
- **Possible future additions (not planned; each would be a new version):**
  - alpha blending;
  - more vertex attributes (normals, a second UV set);
  - 32-bit indices;
  - names or a string table;
  - multi-room levels with level-driven collision.
- **No relationship to Finale:** VRS1 has none to Mary Skelter Finale's formats. A future step that involved authorized game content would need its own approval and documentation; this format would not be extended for it implicitly.

## 13. What this specification does not establish
- **Nothing about Finale:** compatibility with its archives, formats or assets.
- **Unproven items:** the assumptions in 2.2 stay unproven until the stages named there.
- **One device:** performance applies to this Vita, its storage and this stack; the measurements come from one device.
- **CPU-side timings only:** all timings are CPU-side intervals. There is no GPU timer.

## 14. Revision history
| Date | Change |
|---|---|
| 2026-11-15 | First draft for approval (stage 10.1) |
| 2026-11-15 | Second draft. Added: CFP1's full layout, version, size and checksum rules; the complete nesting order; CFP1 in every size and memory figure; a region limit of 8 MiB; per-layer error handling; interrupted loads; the confirmation that ContentFit is reused unchanged; valid and invalid CFP1 examples; a separate application-policy section, so the VRS1-valid 3×3 level is distinguished from ResPipe's room rule; error code 320. Output folder confirmed as `ux0:data/ResPipe/`. Removed the "at most one level per pack" rule, since the room policy already covers levels |
| 2026-11-16 | Approved by the owner. Only the two status lines at the top and this row changed; the format, rules, codes, limits and examples are exactly those of the second draft |
| 2026-11-16 | Clarified 6.1 C.3: format compared in step 7. No change to the format, limits or which packs are accepted |
| 2026-11-16 | Status line updated after stages 10.2 and 10.3. No change to the format, rules, codes or limits |
| 2026-11-16 | Clarified 8.2 and 6.1 A7: a slot's output buffer *U* also holds the table of contents (entry 0). No change to the VRS1 format, codes or limits; a pack whose table of contents is larger than every resource now fits its slots instead of failing at B4 |
| 2026-11-16 | Section 3 lists `rs_loader` (stage 10.4); status line updated. No change to the format, rules, codes or limits |
