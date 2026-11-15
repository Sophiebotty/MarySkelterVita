# GDAT Proto corrections

## Correction 6: G5 "LOAD_SEQ" cases loaded entries in shuffled order

**Date:** 2026-11-15
**Status:** fixed and verified on the PC. **Not yet rerun on the PS Vita** (optional; needs the owner's install approval).
**File changed:** `src/gp_tests.c` only (one line, plus a two-line comment).

### Problem
`run_case()` chose the load order with `c->op[5] == 's'`. Character 5 is `s` in both `load_seq` and `load_shuf`, so every `load_seq` case (G5-04, G5-06 to G5-10) loaded its entries in the shuffled order meant for `load_shuf` (G5-05).

### Evidence
- **Hardware runs `run001` and `run002`** (VPK SHA-256 `44325267…`, 2 sessions × 3 iterations):
  - G5-04 `LOAD_SEQ_MEDIUM` and G5-05 `LOAD_SHUF_MEDIUM` took the same time, for example 339.168 ms and 339.843 ms in run001, iteration 1.
  - G5-10 `LOAD_SEQ_4096` took about 12.4 s. G6-08 `LOAD_4096_SMALL` loads the same file through the same function in index order in about 0.43 s. The difference matches 4,096 out-of-order reads at the measured penalty of about 3.06 ms each.
- **PC load-order harness on the unfixed code:** G5-04 and G5-07 to G5-10 were observed loading in shuffled order, and G5-04's order was identical to G5-05's.

### Fix
```c
/* before */
r->actual = load_all(c->file, c->mode, c->op[5] == 's', 0, &lr);
/* after */
r->actual = load_all(c->file, c->mode, strcmp(c->op, "load_shuf") == 0, 0, &lr);
```

### What did not change
- Fixtures, `expected.txt` and `expected.json`, the fixture generator and checker, and the manifest parser
- Every other GDAT Proto module, `CMakeLists.txt`, the title ID `VDNG00004` and `APP_VER` 01.00
- Phase 8 (`resproto/`), which is compiled unmodified

### Effect on results
- **Pass/fail, status, observations and check values are unchanged.** The list CRC is built in index order, so it never depended on the load order. Verified: all 297 results (99 × 3) are identical before and after the fix.
- **Load order:**
  - G5-04 and G5-07 to G5-10 now load in index order.
  - G5-06 has a single entry, so its order can't change.
  - G5-05 and every G6 test keep exactly the same order as before. The G6 tests pass the order explicitly and were never affected.
- **Hardware timings already recorded:**
  - The G5 timings in `run001` and `run002` measured shuffled loads, despite their names.
  - The G6 timings are unaffected and remain valid.
  - G5-10 is expected to come close to G6-08's 0.43 s on hardware. That is an inference and has not been measured.

### Verification (PC, 2026-11-15)
| Check | Result |
|---|---|
| Load-order harness (suite run with `--wrap` hooks recording the entry order of every multi-entry load), unfixed code | **FAIL as expected:** 5 `LOAD_SEQ` cases shuffled |
| Same harness, fixed code | **PASS:** all 11 multi-entry tests load in the order their names promise; G5-10's order is identical to G6-08's |
| Results before vs after (iteration, id, pass, status, observations, check) | 297 of 297 identical |
| Full suite, AddressSanitizer + UndefinedBehaviorSanitizer + LeakSanitizer build | 99/99 × 3, G7 PASS, 0 bytes leaked, 0 sanitizer diagnostics |
| Full suite, regular build | 99/99 × 3, G7 PASS, 0 bytes leaked |
| GDAT Proto modules with `-Wall -Wextra -Werror` at `-O1`, `-O2`, `-O3` (host gcc) | Clean |
| Manifest parser tests (`tests/host/run_gdatproto_manifest_test.sh`) | Pass in both builds (80 rejections, 8 acceptances each), exit 0 |
| Independent fixture checker | 89/89 cases, 34 files |
| Fixture backup (`backups/phase9-fixtures-pre-corrections-20261114/`) | 36/36 files identical |
| Vita build (`build_gdatproto.sh`) | 0 warnings, 0 errors; only `gp_tests.c` was recompiled. A clean build of all 14 C files also gave 0 warnings and 0 errors |
| VPK package checks | 35/35 pass |
| New VPK | `build-gdatproto/VitaGdatProto.vpk`, 16,533,099 bytes, SHA-256 `934d510358c8334b196fcf0c168c1ee6af461b677b7057589694b3ca5b453e16` |

The load-order harness was a temporary verification tool and isn't part of the project.
