# Vita Resource-Loading Prototype (Phase 8): "Vita ResProto"

**Status:** implemented and built. **Not yet run on the PS Vita, so there are no hardware results.**
**Title ID:** `VDNG00003` (separate from the app `VDNG00001` and VitaBench `VDNG00002`)
**Location:** `resproto/` (sources), `tools/make_resproto_fixtures.py` (PC fixtures), build output `build-resproto/` (git-ignored)

## Scope
- **What it is:** the I/O, validation, decompression and measurement foundation that a future Vita resource loader needs.
- **Data it uses:** only data it **generates itself** on the Vita, plus small fixtures made on a PC with Python's standard zlib. All content is synthetic.
- **What it does not do:** it doesn't read any Mary Skelter file, and it **cannot load Finale archives**. Finale's container formats are only partly understood, and their entry formats are undocumented (`STEAM_ARCHIVE_STRUCTURE.md`, `VITA_PORT_COMPATIBILITY_MAP.md`).
- **Test container format:** `VRP1` is this project's own format, designed to exercise every validation path. It doesn't imitate any game format.

## Architecture
| Module | Purpose |
|---|---|
| `rp_io` | Standard C I/O on `ux0` (`stat`, `fopen`, `fseek`, `fread`; newlib maps these to `sceIo*`). Explicit error codes; 64-bit range checks on every read. |
| `rp_mem` | A tracking allocator (current/peak/refused, leak detection), a 64 MB single-allocation cap, `mallinfo` and `sceKernelGetFreeMemorySize` snapshots, and a largest-malloc probe |
| `rp_container` | The `VRP1` reader with strict validation, plus a builder (used for fixtures) |
| `rp_inflate` | Strict zlib inflate (exact output size, full input consumed); deflate helper; CRC-32 |
| `rp_fixture` | A deterministic generator of all on-device test files |
| `rp_tests` | The test suite (below) |
| `rp_report` | `ux0:data/ResProto/runNNN.csv` and `runNNN.log` |
| `rp_main` | The SDL2/vitaGL shell, using the unmodified `src/graphics/hud2d.c` and `src/input/input.c` |
| `rp_time` | A CPU-side millisecond timer (`SDL_GetPerformanceCounter`) |

**Dependency:** zlib **1.3.2** from the VitaSDK package `zlib` (installed with `vdpm` on 2026-11-14; files `libz.a`, `zlib.h`, `zconf.h`). zlib is third-party software under the zlib licence.

**Core-module portability:** `rp_io`, `rp_container`, `rp_inflate` and `rp_mem` (apart from the Vita memory query) use only standard C/POSIX + zlib (`rp_io` uses POSIX `stat`/`mkdir`), so they can later be built for a PC test. That needs `libc6-dev` in WSL, which isn't installed.

## `VRP1` container specification
All fields are little-endian.

**Header (32 bytes)**
| Offset | Type | Field |
|---|---|---|
| 0 | `char[4]` | `"VRP1"` |
| 4 | u16 | version = 1 |
| 6 | u16 | flags = 0 |
| 8 | u32 | entry_count (≤ 65,536) |
| 12 | u32 | index_offset = 32 |
| 16 | u32 | declared file size |
| 20 | u32 | data_offset = 32 + 24 × entry_count |
| 24 | u32 | CRC-32 of the index bytes |
| 28 | u32 | CRC-32 of header bytes 0–27 |

**Index entry (24 bytes)**
| Field | Meaning |
|---|---|
| u32 offset | Where the entry's data starts |
| u32 stored_size | Bytes stored in the file |
| u32 raw_size | Size after decompression (≤ 16 MB) |
| u32 flags | Bit 0 = zlib-compressed |
| u32 crc32 | CRC-32 of the raw (uncompressed) data |
| u32 reserved | 0 |

Entries must be in ascending offset order and must not overlap. An uncompressed entry requires stored_size = raw_size.

**Validation order:** each step can fail with the error shown, and fails **before any large allocation**.

| Step | Error on failure |
|---|---|
| File at least 32 bytes | `TRUNCATED` |
| Magic | `BAD_MAGIC` |
| Version | `BAD_VERSION` |
| Header CRC | `BAD_HEADER_CRC` |
| Declared size vs actual size | `TRUNCATED` / `SIZE_MISMATCH` |
| Entry count cap | `COUNT` |
| Index bounds | `BAD_INDEX` |
| Index CRC | `BAD_INDEX_CRC` |
| Each entry: flags | `BAD_INDEX` |
| Each entry: range (64-bit arithmetic) | `RANGE` |
| Each entry: order and overlap | `OVERLAP` |
| Each entry: raw-size cap | `LIMIT` |

When an entry is loaded, a stream that doesn't inflate to exactly the declared size fails with `SIZE_MISMATCH`, `TRUNCATED` or `ZLIB_DATA`, and a payload CRC mismatch fails with `CRC`.

## Test suite
The suite runs **three times** per launch, and T14 compares the runs.

**On-device fixtures:** the app writes them to `ux0:data/ResProto/work/`, about 25.3 MB of plain I/O files plus small containers. SELECT on the results screen deletes them.

| ID | Test | Expected |
|---|---|---|
| T1A / T1B | Size of an existing file / a missing file | `OK` / `NOT_FOUND` |
| T2A–T2D | Read whole 4 KB, 1 MB, 8 MB and 16 MB files twice (the second read may come from the OS cache); CRC verified | `OK` + MB/s |
| T3 | 16 MB in 64 KB chunks; CRC verified | `OK` + MB/s |
| T4 | 1,000 random 4 KB reads, each compared against regenerated data | `OK` + µs per read |
| T5A | Valid container: open and load all 48 entries (stored, zlib, and one empty entry) | `OK` + MB/s |
| T5B–T5E | Bad magic, version, header CRC, index CRC | the specific error |
| T6A / T6B | File shorter than declared / longer than declared | `TRUNCATED` / `SIZE_MISMATCH` |
| T7A–T7D | Entry past the end, overlap, offset+size overflow past 32 bits, huge entry count. Must not allocate more than 64 KB while rejecting | `RANGE` / `OVERLAP` / `RANGE` / `COUNT` |
| T8A / T8B | Empty file / zero-entry container | `TRUNCATED` / `OK` |
| T9A–T9D | **Cross-implementation:** inflate three Python-zlib streams and parse a Python-written `VRP1` container | `OK` |
| T10A–T10C | Deflate then inflate 1 MB of text-like, random and zero-filled data on the Vita | `OK` + compression ratio |
| T11A–T11E | Adler-32 trailer flipped, mid-stream byte flipped, truncated stream, declared size too small, declared size too large | `ZLIB_DATA` / any error / `TRUNCATED` / `SIZE_MISMATCH` ×2 |
| T11F | A container declaring a 2 GB entry ("decompression bomb") | `LIMIT`, refused at open |
| T12A–T12C | Inflate speed for each payload type (average of 4 runs) | `OK` + MB/s |
| T13 | Tracked bytes back to 0; peak, free memory and largest `malloc` recorded | `OK` |
| T14 | The 3 runs agree on pass/fail, error codes and check values (CRCs, sizes) | `OK` |

**PC fixtures:**
- `tools/make_resproto_fixtures.py` writes `resproto/fixtures/` (`pc_text.z`, `pc_random.z`, `pc_zero.z`, `pc_container.vrp`, `manifest.txt`). The build packs them into the VPK.
- On the Vita the app reads them from `ux0:app/VDNG00003/fixtures/`, falling back to `app0:fixtures/`. The log records which path worked.

## How to build, install and run
1. In WSL, from the project root:
```bash
bash resproto/build_resproto.sh
```
2. Install `build-resproto/VitaResProto.vpk` with VitaShell. It installs alongside the working app and VitaBench.
3. Launch "Vita ResProto". It writes its test files, runs the suite three times, and shows the results.
   - **O** runs it again (if any test file is missing, for example after SELECT, it regenerates them first and says so on screen)
   - **TRIANGLE** regenerates the files and runs again
   - **SELECT** deletes the generated files
   - **START** exits
4. Copy `ux0:data/ResProto/runNNN.csv` and `runNNN.log` to your PC (VitaShell FTP or USB) and send them.

## Verification status
| Item | Status |
|---|---|
| Builds with the VitaSDK, `-Wall -Wextra` | **Yes**: 0 warnings, 0 errors |
| VPK structure | **Yes**: ZIP integrity OK; `TITLE_ID` `VDNG00003`; fixtures included |
| zlib linked | **Yes**: `inflate`, `deflate`, `crc32` and `compress2` present in the ELF |
| No immediate-mode GL | **Yes**: source and objects checked |
| No game paths or formats in the prototype | **Yes**: only a disclaimer comment mentions Finale |
| PC fixtures | **Yes**: checked separately with Python. 3 streams decompress to the manifest's size and CRC; the container passes 13/13 specification checks |
| Prototype logic run anywhere (Vita or PC) | **No**: the C code has only been compiled. No native PC compiler is available (WSL lacks `libc6-dev`) |
| Hardware results (speeds, memory, pass/fail) | **None yet** |

## Results (to be filled from the Vita CSV; leave blank until measured)
| Test | Result | Value | Notes |
|---|---|---|---|
| T2D read 16 MB (first / second) | | | |
| T3 chunked 16 MB | | | |
| T4 random reads | | | |
| T5A container load | | | |
| T12A–C inflate speed | | | |
| T13 peak / largest malloc | | | |
| T14 repeatability | | | |
| Pass count | | | |

## Risks and limitations
1. **Storage dependence:** read speeds depend on the card (Sony memory card vs SD2Vita) and on caching. The first read happens right after writing, so it may also come from the cache. A true cold read would need a reboot.
2. **Heap size:** newlib's default heap may limit large allocations. T2D (16 MB) fails with `NOMEM` if so, and T13's probe records the largest successful `malloc`. Adjusting the heap is a separate decision.
3. **CPU-side timing only;** synchronous I/O only (asynchronous `sceIoReadAsync` is a later extension).
4. **Writes to `ux0`:** about 26 MB per generation. Running out of space is reported, not hidden.
5. **Fixture location:** whether the app can read `ux0:app/VDNG00003/` directly is unverified; there's an `app0:` fallback.
6. **Scope:** passing tests show that the Vita-side foundation works on synthetic data. They prove **nothing** about Finale's formats. Loading authorized Finale content still needs Idea Factory's permission and format documentation.
