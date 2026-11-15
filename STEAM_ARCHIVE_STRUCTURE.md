# Mary Skelter Finale (Steam): `masterWindows` Archive Structure

**Phase:** 7, read-only structural investigation  
**Date:** 2026-11-14  
**Location inspected:** `C:\Program Files (x86)\Steam\steamapps\common\Mary Skelter Finale\masterWindows\`  
**Vita project state:** `314bd81`, not modified by this investigation

### Method and limits
- Files were opened **read-only** from WSL with Python (`open(..., "rb")`).
- **What was read:**
  - File sizes and the first bytes of every file
  - The 8-byte header and index table of each `.dat`
  - The 128-byte header and `BILZ` header fields of each `.csh`
  - The 16-byte header and trailing directory of each `.bra`
  - The MP4 box tree (`ftyp` and `moov` metadata only)
  - The first 4–0x92 bytes of a **sample** of archive entries, to read their type tags and wrapper headers
- **What was not done:**
  - Nothing was decompressed, decoded, extracted, unpacked, decrypted or copied.
  - Nothing was executed, and no media was played.
  - No file in the Steam folder was written, renamed, moved or deleted.
  - Large archives were not copied into the project.
- **Verification:** before/after snapshots of all 288 entries in the game folder (sizes and modification times) match; see §9.

### Labels
| Label | Meaning |
|---|---|
| **CONFIRMED** | Directly observed in the bytes, and consistent across every file checked |
| **STRONG HYPOTHESIS** | Fits all observed data, but the meaning isn't proven |
| **UNKNOWN** | Not determinable without decompressing or decoding content, or without documentation |

---

## 1. Inventory (CONFIRMED)

`masterWindows` contains **77 files and 2 subfolders**: 37 `.dat`, 37 `.csh` and 3 `.bra`, plus 22 + 4 `.mp4` in the subfolders. Total: **9,151,838,174 bytes**.

### 1.1 GxArchivedFile*.dat and GxArchivedTable*.csh
| # | `.dat` size (bytes) | `.csh` size (bytes) | DAT entries |
|---|---|---|---|
| 051 | 2,469,183 | 1,379 | 107 |
| 052 | 347,230,310 | 15,652 | 1,567 |
| 053 | 164,588,907 | 4,532 | 467 |
| 054 | 497,674,860 | 4,506 | 471 |
| 055 | 128,997,856 | 14,833 | 1,456 |
| 056 | 5,721,584 | 1,735 | 155 |
| 057 | 363,207,436 | 13,133 | 1,169 |
| 058 | 316,714,682 | 9,620 | 946 |
| 059 | 1,235,089,747 | 24,928 | 2,473 |
| 060 | 169,167,980 | 7,558 | 696 |
| 061 | 257,221,602 | 2,665 | 263 |
| 062 | 1,860,164 | 181 | 1 |
| 063 | 73,072,672 | 1,369 | 124 |
| 064 | 55,946,416 | 11,599 | 1,001 |
| 065 | 20,655,007 | 5,494 | 479 |
| 066 | 729,713 | 212 | 3 |
| 067 | 25,111,984 | 648 | 43 |
| 068 | 29,748,371 | 588 | 45 |
| 069 | 75,584,317 | 1,469 | 136 |
| 070 | 55,765,655 | 11,610 | 1,001 |
| 071 | 20,507,700 | 5,490 | 479 |
| 072 | 688,305 | 212 | 3 |
| 073 | 194,201,248 | 2,442 | 236 |
| 074 | 14,481,761 | 462 | 24 |
| 075 | 1,860,164 | 181 | 1 |
| 076 | 120,233,422 | 1,557 | 152 |
| 077 | 29,671,194 | 585 | 45 |
| 078 | 62,347,494 | 1,470 | 136 |
| 079 | 57,092,363 | 11,608 | 1,001 |
| 080 | 21,337,230 | 5,487 | 479 |
| 081 | 928,895 | 224 | 4 |
| 082 | 82,053,497 | 1,525 | 132 |
| 083 | 15,083,989 | 462 | 24 |
| 084 | 7,117,587 | 181 | 1 |
| 085 | 128,765,548 | 1,632 | 158 |
| 086 | 70,182 | 262 | 7 |
| 088 | 930,529,217 | 36,538 | 3,510 |

### 1.2 Sound archives
| File | Size (bytes) |
|---|---|
| `Sound.bra` | 1,119,552,117 |
| `Sound_EN.bra` | 209,778,703 |
| `Voice_IL.bra` | 90,931,665 |

**Name correction (CONFIRMED):** the third archive is named **`Voice_IL.bra`**, not `Voice_JL.bra`.

### 1.3 `MOVIE_EN/` (22 files) and `MOVIE_LOGO/` (4 files): see §7.

---

## 2. Sequence gaps (CONFIRMED)
- `GxArchivedFile*.dat`: 37 files, numbered **051–088**; **087 is missing**.
- `GxArchivedTable*.csh`: 37 files, numbered **051–088**; **087 is missing**.
- **Pairing:** every DAT has a CSH with the same number, and every CSH has a DAT.
- **Movie names:** `MOVIE_EN` has 100001–100011, 400001–400010 **without 400006**, 600000 and 700000. `MOVIE_LOGO` has 500001, 500002, 500004 (**no 500003**) and `Ghostlight.mp4`.
- **Numbering below 051** (000–050) isn't present in this folder (CONFIRMED). Whether it exists elsewhere or was never used is **UNKNOWN**.
- **Why 087, 400006 and 500003 are absent** (removed content, platform-specific content, or skipped IDs): **UNKNOWN**.

---

## 3. File signatures (CONFIRMED)
| Files | First bytes | Reading |
|---|---|---|
| All 37 `.dat` | `47 44 41 54` | ASCII `GDAT` |
| All 37 `.csh` | `00 68 73 63 00 00 00 02 00 00 00 0D 00 00 00 02` | `\0hsc` (reads as `csh` reversed), then the big-endian fields 2, 13, 2 |
| All 3 `.bra` | `50 44 41 00 02 00 00 00` | ASCII `PDA\0`, then u32 2 |
| 20 `.mp4` (100001–100011, 400001–400010, `Ghostlight.mp4`) | `00 00 00 18 66 74 79 70 6D 70 34 32` | ISO-BMFF `ftyp`, brand `mp42` |
| 6 `.mp4` (600000, 700000, 500001, 500002, 500004) | `00 00 00 20 66 74 79 70 69 73 6F 6D` | ISO-BMFF `ftyp`, brand `isom` |

**Entropy (CONFIRMED):** samples from the middle of the DAT files measure 7.59–7.96 bits/byte, which is near-random. That's consistent with compressed (or encrypted) payloads. §4 shows the wrappers carry headers consistent with zlib.

None of `GDAT`, `\0hsc`/`BILZ` or `PDA\0` matched a publicly documented format in a web search. The only related public hit was a game file manifest using the same `GxArchivedFile###.dat` naming. **Public specification: UNKNOWN.**

---

## 4. `GxArchivedFile*.dat` structure

### 4.1 Header and index (CONFIRMED in all 37 files)
```
offset 0   char[4]  "GDAT"
offset 4   u32 LE   entry_count
offset 8   entry_count × { u32 LE data_offset, u32 LE data_size }
```
Checked in every file:
- `data_offset` of entry 0 equals 8 + 8 × `entry_count`, so the data starts right after the index.
- Entries are **ordered and non-overlapping**: each index is already sorted by offset (37/37), and each entry ends at or before the next one's offset.
- **Gap-free (verified separately):** in a second read-only pass over the index tables, every entry's `data_offset` was compared with the previous entry's `data_offset + data_size`. All **18,958** consecutive pairs across the 37 files differ by exactly **0**. The first entry starts exactly at the end of the index, and the last ends exactly at end of file.
- The last entry ends **exactly at end of file** (0 trailing bytes in all 37).
- **Alignment:** none in general. The single-entry archives (062, 075, 084) happen to start at offset 16.

### 4.2 Entry type tags (CONFIRMED for the sample)
Every sampled entry (up to about 200 per archive) begins with `00` followed by three ASCII letters. Read in reverse, these give a three-letter type code, the same convention as the `csh` magic:

| Tag (reversed) | Sampled count |
|---|---|
| `CL3` | 2,529 |
| `tid` | 1,611 |
| `tex` | 503 |
| `mdl` | 147 |
| `mdc` | 145 |
| `spk` | 63 |
| `mpk` | 54 |
| `csh` | 49 |
| `psb` | 12 |
| `enc` | 8 |
| `dat` | 6 |
| `vsb` | 5 |
| `bin` | 4 |
| `ffu` | 3 |
| `olf` | 1 |

**STRONG HYPOTHESIS:** these are asset-type codes, for example `tex` = texture and `mdl` = model. **UNKNOWN:** what `CL3`, `tid`, `mdc`, `spk`, `mpk`, `psb`, `enc`, `vsb`, `ffu` and `olf` mean. `enc` might suggest "encoded" or "encrypted", but that's unverified.

### 4.3 Entry wrapper (CONFIRMED for every sampled entry, n = 1,569)
- **Header:** each sampled entry has a **128-byte header** (tag at offset 0).
- **`BILZ` block at offset 0x80:** big-endian `uncompressed_size`, `compressed_size` and the constant `7`.
- **zlib-like stream:** the two bytes at 0x90 are `78 DA`, **consistent with** a zlib header (deflate, maximum compression level). This is a header signature only: no stream was decompressed or checksum-validated, so it doesn't prove every complete stream is valid zlib.
- **Compression ratios** (uncompressed ÷ compressed) from those headers: min 1.00, median 5.26, max 506.21.

**STRONG HYPOTHESIS:** game assets are stored **zlib-compressed** inside a `BILZ` wrapper, not encrypted at the wrapper level; the type tags and sizes are plain. This rests on the `78 DA` header signature and the plausible size fields, not on decompression. **UNKNOWN:** whether the complete streams decompress and validate, and whether the decompressed contents are further encoded or encrypted. Nothing was decompressed.

### 4.4 Duplicate and parallel archives
- **CONFIRMED:** `GxArchivedFile062.dat` and `GxArchivedFile075.dat` are **byte-identical** (same size, SHA-256 `25e4eeadc28879d9…`). Their `.csh` files differ by a few bytes (SHA-256 `7839bdd9…` vs `7ed3785f…`).
- **CONFIRMED:** several archives have **identical entry counts but different sizes**, forming parallel groups:

| Entry count | Archives |
|---|---|
| 1,001 | 064 / 070 / 079 |
| 479 | 065 / 071 / 080 |
| 136 | 069 / 078 |
| 45 | 068 / 077 |
| 24 | 074 / 083 |
| 3 | 066 / 072 |
| 1 | 062 / 075 / 084 |

- **STRONG HYPOTHESIS:** 062–069, 070–078 and 079–086 are **parallel sets** of the same resource layout with different content (for example per-language, per-region or per-content-pack variants). **UNKNOWN:** what distinguishes the sets.

---

## 5. `GxArchivedTable*.csh` structure (metadata only; nothing decompressed)

### 5.1 Layout (CONFIRMED in all 37 files)
```
0x00  "\0hsc"            (csh, reversed)
0x04  u32 BE 2
0x08  u32 BE 13
0x0C  u32 BE 2
0x10 … 0x7F  zero, except byte 0x2B = 01
0x80  "BILZ"
0x84  u32 BE uncompressed_size
0x88  u32 BE compressed_size  ( == file_size − 0x90 in all 37 )
0x8C  u32 BE 7
0x90  stream starting 78 DA in all 37 (consistent with a zlib header; not decompressed or validated)
```
The `.csh` header and `BILZ` wrapper are **the same format** as the wrapper on DAT entries (§4.3). The DAT archives also contain 49 sampled entries tagged `csh`.

### 5.2 Relationship to the DAT (CONFIRMED arithmetic)
In **all 37 pairs**: `uncompressed_size` = **12 + 28 × (entry_count of the same-numbered DAT)**. Examples: 062 → 40 = 12 + 28 × 1; 052 → 43,888 = 12 + 28 × 1,567; 088 → 98,292 = 12 + 28 × 3,510.

### 5.3 Interpretation
| Question | Answer | Label |
|---|---|---|
| Is each CSH tied to its same-numbered DAT? | Yes: the table size scales exactly with that DAT's entry count | CONFIRMED (arithmetic) |
| Does the CSH hold one fixed-size **28-byte record per DAT entry**, after a 12-byte header? | Fits all 37 files | STRONG HYPOTHESIS |
| Do the records hold offsets, sizes, hashes or IDs? | The DAT already holds offset and size, so the records likely hold **lookup data** (for example name hashes or resource IDs) | STRONG HYPOTHESIS (contents not decoded) |
| Readable resource names? | **None in the stored bytes.** The table body begins with a zlib-consistent header and has high entropy; the only printable runs look like compressed-data noise | CONFIRMED (no plaintext names) / UNKNOWN (decompressed content) |
| Encryption? | No sign of it at the container level: a zlib-consistent header (`78 DA`) and plausible size fields. Streams weren't decompressed or validated | STRONG HYPOTHESIS |

---

## 6. `.bra` sound archives

### 6.1 Header and directory (CONFIRMED in all 3)
```
0x00  char[4] "PDA\0"
0x04  u32 LE  2              (version?)
0x08  u32 LE  directory_offset
0x0C  u32 LE  record_count
0x10  entry data: ordered, non-overlapping and gap-free (every record's data_offset
      equals the previous record's data_offset + stored_size, verified for all
      consecutive pairs), ending exactly at directory_offset
directory_offset … EOF:  record_count variable-length records
```
Directory record, parsed for every record; each directory decodes to its last byte:
```
u32 a            (see 6.3)
u32 b
u32 stored_size  (bytes occupied in the data area, incl. a 16-byte entry header)
u32 size         (declared size of the resource)
u16 name_len     (multiple of 4; name is NUL-padded)
u16 c            (0x20 in every record shown in the directory heads inspected)
u32 data_offset
char name[name_len]
```

| Archive | Records | Directory bytes | Parsed exactly | Data ends at directory |
|---|---|---|---|---|
| `Sound.bra` | 45,425 | 2,112,136 | Yes | Yes (1,117,439,981) |
| `Sound_EN.bra` | 16,728 | 824,500 | Yes | Yes (208,954,203) |
| `Voice_IL.bra` | 3,630 | 174,240 | Yes | Yes (90,757,425) |

### 6.2 Contents (CONFIRMED from directory names and entry signatures)
| Archive | Top-level folders (records) | Extensions |
|---|---|---|
| `Sound.bra` | `VOICE\` (about 22,668 `.ogg`), `VOICE\EVENT\…` (22,662, no extension, e.g. `voice\event\lip\…`), `BGM` 56, `BGM_F` 31, `JINGLE` 6, `SE` 2 | `.ogg` 22,761 · no extension 22,662 · `.acb` 2 |
| `Sound_EN.bra` | `VOICE_EN\…` (e.g. `VOICE_EN\0000002.ogg`) | `.ogg` 8,197 · no extension 8,104 · `.acb` 427 |
| `Voice_IL.bra` | `VOICE_IL\…` (e.g. `VOICE_IL\0014001.ogg`) | `.ogg` 1,815 · no extension 1,815 |

The `VOICE\` count is derived: 45,425 − 22,662 − 56 − 31 − 6 − 2.

**Entry encoding**, from about 300 sampled entries per archive:
- **`.ogg` records:** `stored_size − size = 16`, and the 16-byte entry header is (size, size, 0, 0). The payload at +16 starts with **`OggS`** (CONFIRMED in all 453 sampled `.ogg` entries). These are **Ogg streams stored without additional BRA-level compression**: stored size = size + the 16-byte entry header (CONFIRMED container). The Ogg audio itself is still a compressed audio format; the codec inside (Vorbis or Opus) wasn't checked: **UNKNOWN**.
- **Extensionless records** (for example `…\lip\…`):
  - `stored_size` is **smaller** than `size`, and the payload starts with bytes like `8D 9A 3D 6B`.
  - **STRONG HYPOTHESIS:** compressed per-line lip-sync or animation data paired with voice lines. Roughly one extensionless record per voice `.ogg` in every archive.
  - **UNKNOWN:** the compression method and data format.
- **`.acb` records:** **STRONG HYPOTHESIS:** CRI ADX2 "Atom Cue Sheet Binary", based on the extension and the `CriAtomCueSheetBinary` string in the executable (`TECHNICAL_AUDIT.md`). Their entry signatures weren't sampled separately: **UNKNOWN**.

### 6.3 Directory fields not identified
- **Field `a`:** values range 1,530,197,589 – 1,686,041,705. **STRONG HYPOTHESIS:** Unix timestamps, which would read as 2018-06-28 to 2023-06-06 across the three archives. That's plausible for a 2020–2023 game reusing older voice assets, but unproven.
- **Field `b` and field `c`:** meaning **UNKNOWN**.
- **`Voice_IL`:** what "IL" stands for is **UNKNOWN**.

---

## 7. Movie folders (CONFIRMED from MP4 metadata; no media decoded)

| File | Size (bytes) | Brand | Box order | Duration | Tracks |
|---|---|---|---|---|---|
| MOVIE_EN/100001–100011 (11 files) | 120,995,280 – 121,065,624 | mp42 | ftyp/moov/uuid/mdat | 306.2 s each | H.264 (`avc1`) 1920×1088 + AAC (`mp4a`) |
| MOVIE_EN/400001 | 37,959,462 | mp42 | ftyp/moov/uuid/mdat | 94.7 s | avc1 1920×1088 + mp4a |
| MOVIE_EN/400002 | 129,624,488 | mp42 | same | 328.3 s | same |
| MOVIE_EN/400003 | 52,855,797 | mp42 | same | 133.0 s | same |
| MOVIE_EN/400004 | 184,452,002 | mp42 | same | 469.0 s | same |
| MOVIE_EN/400005 | 39,073,527 | mp42 | same | 98.5 s | same |
| MOVIE_EN/400007 | 37,939,145 | mp42 | same | 94.7 s | same |
| MOVIE_EN/400008 | 129,630,601 | mp42 | same | 328.3 s | same |
| MOVIE_EN/400009 | 35,975,650 | mp42 | same | 91.1 s | same |
| MOVIE_EN/400010 | 110,432,819 | mp42 | same | 279.7 s | same |
| MOVIE_EN/600000 | 51,551,961 | isom | ftyp/free/mdat/moov | 86.7 s | avc1 1920×1088 + mp4a |
| MOVIE_EN/700000 | 70,233,069 | isom | same | 86.7 s | same |
| MOVIE_LOGO/500001 | 2,333,360 | isom | ftyp/free/mdat/moov | 7.2 s | avc1 1920×1088 + mp4a |
| MOVIE_LOGO/500002 | 1,370,615 | isom | same | 7.0 s | same |
| MOVIE_LOGO/500004 | 43,834 | isom | same | 4.0 s | avc1 1920×1088 (no audio track) |
| MOVIE_LOGO/Ghostlight.mp4 | 2,918,894 | mp42 | ftyp/moov/uuid/free/mdat | 6.0 s | avc1 **1920×1080** + mp4a |

**Totals:** `MOVIE_EN` 2,211,176,715 bytes; `MOVIE_LOGO` 6,666,703 bytes (sums of the file sizes, measured).

**Observations:**
- **Standard, unencrypted containers:** these are ordinary ISO-BMFF MP4s with H.264 and AAC; the containers are readable and not encrypted. That's consistent with the Media Foundation imports noted in `TECHNICAL_AUDIT.md`. **The executable's CRI Sofdec2 strings don't apply to these files.**
- **1920×1088:** the usual H.264 coded height for 1080p (a multiple of 16). The actual display crop wasn't checked (STRONG HYPOTHESIS: 1920×1080 displayed).
- **Repeated durations:** 100001–100011 are all 306.2 s; 400001 and 400007 are both 94.7 s; 400002 and 400008 are both 328.3 s; 600000 and 700000 are both 86.7 s. Whether they're variants of the same content (for example different language tracks or endings) is **UNKNOWN**.
- **Vita relevance (INFERRED):** about 2.1 GB of 1080p H.264 would need re-encoding to the Vita's resolution and decoder limits. Those limits haven't been measured (see `MARY_SKELTER_VITA_FEASIBILITY.md` §4).

---

## 8. Summary by confidence

### Confirmed observations
1. **Inventory:** 77 files and 2 subfolders, 9,151,838,174 bytes. DAT/CSH numbers run 051–088 with **087 missing** from both. Movie IDs 400006 and 500003 are absent. The third sound archive is **`Voice_IL.bra`**.
2. **DAT index:** `GDAT` + u32 count + (u32 offset, u32 size) × count. Entries are ordered, non-overlapping and **gap-free**: all 18,958 consecutive pairs have offset = previous end. They fill each file exactly, in all 37.
3. **Common wrapper:** sampled DAT entries and all CSH files share the same container: a 128-byte header with a reversed 3-letter type tag, then `BILZ` sizes, then a stream starting `78 DA` (**consistent with zlib**; not decompressed or validated).
4. **CSH ↔ DAT link:** each CSH's uncompressed table size = 12 + 28 × its DAT's entry count, in all 37 pairs.
5. **Duplicates:** DAT 062 and 075 are byte-identical, and several archive groups share identical entry counts.
6. **BRA layout:** `PDA\0` v2 archives with a trailing directory of named records that parses exactly. They contain about 32,700 **Ogg files stored without additional BRA-level compression** plus about 32,500 compressed extensionless companion records, and 429 `.acb` files. Data areas are gap-free. Folder names: `BGM`, `BGM_F`, `JINGLE`, `SE`, `VOICE`, `VOICE_EN`, `VOICE_IL`.
7. **Movies:** 26 standard MP4 files, H.264 1920×1088 (Ghostlight 1920×1080) with AAC audio except 500004; durations as tabulated.

### Strong hypotheses
- **Type tags:** DAT entry tags are asset-type codes (`tex` texture, `mdl` model, etc.).
- **Compression, not encryption:** archive contents are zlib-compressed (based on `78 DA` headers and size fields; not decompressed), with no encryption at the container level.
- **CSH records:** each CSH holds 28-byte lookup records (probably IDs or hashes) for its DAT.
- **Parallel sets:** 062–069, 070–078 and 079–086 are parallel resource sets.
- **Lip-sync data:** extensionless BRA records are compressed lip-sync data paired with voice lines.
- **`.acb` files** are CRI ADX2 cue sheets.
- **Timestamps:** BRA field `a` is a Unix timestamp.
- **1080p:** the movies display at 1920×1080.

### Unknowns requiring further research
- The meaning of most type tags (`CL3`, `tid`, `mdc`, `spk`, `mpk`, `psb`, `enc`, `vsb`, `ffu`, `olf`).
- The fields in the CSH header and the 28-byte records, and how the game maps names to entries.
- Whether decompressed assets are further encoded or encrypted, and their internal formats (texture and model formats).
- Why archives 000–050 and 087 are absent; what separates the parallel archive sets.
- BRA fields `b` and `c`; the meaning of "IL"; the lip-data compression; the audio codec inside the Ogg streams.
- The purpose of the repeated-duration movies.
- **Any public specification for GDAT, BILZ, CSH or PDA:** none found ([one search](#sources)).

**Note on next steps:** answering most of these unknowns would require decompressing or decoding game content. That falls outside this read-only investigation and outside the permissions discussed in `TECHNICAL_AUDIT.md` §8 and `MARY_SKELTER_VITA_FEASIBILITY.md` §9 and §12. The rights holder's format documentation (feasibility report §12, item 7) is the legitimate source.

---

## 9. Integrity verification
- **Steam game folder:** a snapshot of all 288 entries (relative path, size, last-write time, attributes) was taken before and after the investigation. Result: **288 / 288 entries, 0 differences**. A sample `.csh` hash (062) was also re-checked and is unchanged.
- **Game executable:** its SHA-256 still matches the audit (`5C16ABFD…`).
- **Vita project:** the only file added is this report. No source, build file, benchmark CSV, existing document or Git commit was changed.

---

## Sources
- Local: read-only inspection of the files listed above; `TECHNICAL_AUDIT.md`; `MARY_SKELTER_VITA_FEASIBILITY.md`.
- Web search (2026-11-14) for the archive names and magic values. **No public specification was found.** Consulted results:
  - GOG manifest listing `GxArchivedFile###.dat` file names: <https://www.gogdb.org/manifest/874184bdfd42e3c9150b72f255b0916e>
  - Unrelated "GGXArchiver" format notes, not this format: <https://datacrystal.tcrf.net/wiki/Strike_Witches:_Silver_Wing/Notes>
