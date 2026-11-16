#!/bin/sh
# Vita ResPipe stage 10.3 PC tests: src/resource/rs_format.c.
# Builds and runs, in a private temporary directory:
#   1. warnings-as-errors compiles: rs_format.c at -O0..-O3 with -Wall -Wextra
#      -Wshadow -Wconversion -Wsign-conversion -Wcast-qual -Wstrict-prototypes,
#      and the test sources with -Wall -Wextra
#   2. the test program with AddressSanitizer + UndefinedBehaviorSanitizer
#      (-fno-sanitize-recover=all, leak checking on): unit checks, every valid
#      fixture, the 50 section 10 cases, the room mesh against the unmodified
#      level_mesh_build(), and every header byte flipped (file and entry level)
#   3. compare_rs_results.py: every result against expected.json,
#      RESOURCE_PIPELINE.md and the independent Python checker
#   4. git: no tracked file changed (prototypes, ContentFit, renderer, spec)
# The sanitizer build is mandatory; any sanitizer report fails the run.
# Exit status is non-zero if anything fails.
#
# usage: sh tests/host/run_respipe_host_tests.sh   (from WSL, project root)
# Needs respipe/fixtures (python3 -I -B tools/make_respipe_fixtures.py).

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
CC=${CC:-gcc}
T=$(mktemp -d /tmp/respipe-host-test.XXXXXX) || exit 1
FAILED=0

cleanup() {
    status=$?
    trap - EXIT HUP INT TERM
    rm -rf -- "$T" || true
    [ "$status" -ne 0 ] && echo "respipe host tests FAILED (exit status $status)" >&2
    exit "$status"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

fail() { echo "FAIL: $*"; FAILED=1; }

FIX="$ROOT/respipe/fixtures"
[ -f "$FIX/rs_fixtures.h" ] || { echo "fixtures missing: run python3 -I -B tools/make_respipe_fixtures.py" >&2; exit 2; }
python3 -I -B "$ROOT/tools/check_respipe_fixtures.py" --fixture-dir "$FIX" > "$T/fixcheck.txt" 2>&1 || {
    tail -20 "$T/fixcheck.txt"; echo "fixture check failed" >&2; exit 3; }
echo "fixture check: $(tail -1 "$T/fixcheck.txt")"

mkdir -p "$T/zinc"
cp /usr/local/vitasdk/arm-vita-eabi/include/zlib.h /usr/local/vitasdk/arm-vita-eabi/include/zconf.h "$T/zinc/" || exit 4
ZLIB=/usr/lib/x86_64-linux-gnu/libz.so.1

# The start cell the policy uses must be the main app's (src/main.c, read only).
SX=$(sed -n 's/^#define START_CELL_X[[:space:]]*\([0-9][0-9]*\).*/\1/p' "$ROOT/src/main.c")
SZ=$(sed -n 's/^#define START_CELL_Z[[:space:]]*\([0-9][0-9]*\).*/\1/p' "$ROOT/src/main.c")
[ -n "$SX" ] && [ -n "$SZ" ] || { echo "START_CELL_X/Z not found in src/main.c" >&2; exit 5; }
echo "src/main.c start cell: $SX, $SZ"

TH="$ROOT/tests/host/respipe"
INC="-I$TH/stub -I$TH -I$ROOT/src -I$ROOT/src/resource -I$ROOT/contentfit/src -I$ROOT/gdatproto/src \
-I$ROOT/resproto/src -I$FIX -I$T/zinc"
DEFS="-DMAIN_START_X=$SX -DMAIN_START_Z=$SZ"
# unmodified modules linked into the test: GDAT Proto, Phase 8, ContentFit, renderer
REUSED="$ROOT/gdatproto/src/gp_common.c $ROOT/gdatproto/src/gp_gdat.c $ROOT/gdatproto/src/gp_wrap.c \
$ROOT/resproto/src/rp_io.c $ROOT/resproto/src/rp_inflate.c \
$ROOT/contentfit/src/cf_pack.c $ROOT/contentfit/src/cf_zinflate.c $ROOT/contentfit/src/cf_common.c \
$ROOT/src/gameplay/room_map.c $ROOT/src/graphics/level_mesh.c"
TESTS="$TH/test_rs_format.c $TH/rs_host_pipe.c $TH/rs_host_plat.c $TH/rs_gl_capture.c"

echo "### 1. warnings-as-errors compiles"
STRICT="-Wall -Wextra -Werror -Wshadow -Wconversion -Wsign-conversion -Wcast-qual -Wstrict-prototypes"
for opt in -O0 -O1 -O2 -O3; do
    $CC -std=gnu99 $opt $STRICT $INC -c "$ROOT/src/resource/rs_format.c" -o "$T/w.o" 2> "$T/w.txt" ||
        { cat "$T/w.txt"; fail "$opt rs_format.c"; }
    for f in $TESTS; do
        $CC -std=gnu99 $opt -Wall -Wextra -Werror $DEFS $INC -c "$f" -o "$T/w.o" 2> "$T/w.txt" ||
            { cat "$T/w.txt"; fail "$opt $(basename "$f")"; }
    done
    echo "    $opt done"
done

echo "### 2. sanitizer build and run (ASan + UBSan, mandatory)"
SAN="-fsanitize=address,undefined -fno-sanitize-recover=all"
objs=""
for f in "$ROOT/src/resource/rs_format.c" $REUSED $TESTS; do
    o="$T/$(basename "$f" .c).o"
    $CC -std=gnu99 -O1 -g -fno-omit-frame-pointer $SAN $DEFS $INC -c "$f" -o "$o" ||
        { echo "sanitizer build of $(basename "$f") FAILED" >&2; exit 6; }
    objs="$objs $o"
done
# rp_mem.c uses mallinfo, deprecated in glibc only
$CC -std=gnu99 -O1 -g -fno-omit-frame-pointer -Wno-deprecated-declarations $SAN $INC \
    -c "$ROOT/resproto/src/rp_mem.c" -o "$T/rp_mem.o" || { echo "sanitizer build of rp_mem.c FAILED" >&2; exit 6; }
$CC $SAN $objs "$T/rp_mem.o" "$ZLIB" -lm -o "$T/test_rs_format" || { echo "sanitizer link FAILED" >&2; exit 6; }
echo "    test_rs_format (ASan+UBSan) built"

W="$T/work"
mkdir -p "$W/app0:fixtures"
for f in "$FIX"/*.gdat; do ln -s "$f" "$W/app0:fixtures/$(basename "$f")"; done
ln -s "$FIX/bad" "$W/app0:fixtures/bad"
export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:detect_stack_use_after_return=1:strict_string_checks=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
start=$(date +%s)
( cd "$W" && "$T/test_rs_format" "$T/results.txt" ) > "$T/test.txt" 2>&1
st=$?
diag=$(grep -c -E 'ERROR: (AddressSanitizer|LeakSanitizer)|runtime error:|SUMMARY: (Address|Undefined|Leak)' "$T/test.txt")
grep -v '^  [ICPW]-[0-9][0-9] ' "$T/test.txt" | sed 's/^/    /'
echo "    test_rs_format: exit $st, sanitizer reports $diag, $(( $(date +%s) - start )) s"
if [ "$st" -ne 0 ] || [ "$diag" -ne 0 ]; then
    grep -E 'FAIL|ERROR|runtime error|SUMMARY' "$T/test.txt" | head -40
    fail "test_rs_format"
fi
cp "$T/test.txt" "${RS_HOST_REPORT_DIR:-$T}/respipe_10_3_test.txt" 2>/dev/null || true
cp "$T/results.txt" "${RS_HOST_REPORT_DIR:-$T}/respipe_10_3_results.txt" 2>/dev/null || true

echo "### 3. results against expected.json, RESOURCE_PIPELINE.md and the Python checker"
start=$(date +%s)
python3 -I -B "$TH/compare_rs_results.py" --results "$T/results.txt" --fixture-dir "$FIX" > "$T/compare.txt" 2>&1 ||
    fail "compare_rs_results.py"
sed 's/^/    /' "$T/compare.txt" | head -60
echo "    ($(( $(date +%s) - start )) s)"

echo "### 4. git: no tracked file changed"
if git -C "$ROOT" diff --quiet HEAD --; then
    echo "    no tracked file changed (prototypes, ContentFit, renderer, specification)"
else
    git -C "$ROOT" diff --stat HEAD
    fail "tracked files changed"
fi

if [ "$FAILED" -ne 0 ]; then
    echo "respipe host tests: FAILURES above"
    exit 1
fi
echo "ALL RESPIPE 10.3 HOST TESTS PASSED"
exit 0
