#!/bin/sh
# ContentFit PC tests (Stage 4). Builds and runs, in a private temporary
# directory:
#   1. -Wall -Wextra -Werror compiles of every ContentFit source at -O1/-O2/-O3
#   2. unit tests          (AddressSanitizer + UndefinedBehaviorSanitizer)
#   3. unit tests again    (ThreadSanitizer: the background reader thread)
#   3b. shader-cache redirect: contentfit/src/cf_iowrap.c linked with
#      -Wl,--wrap=sceIoOpen,--wrap=sceIoMkdir as on the Vita (ASan+UBSan)
#   4. scene check         (stand-in GL; design-table counts) under ASan+UBSan
#   5. whole-suite dry runs in five modes under ASan+UBSan, then the
#      independent analyzer on every run's files
# Sanitizer builds are mandatory: if one cannot be built, the script fails.
# Exit status is non-zero if anything fails (also under dash).
#
# usage: sh tests/host/run_contentfit_host_tests.sh   (from the project root)
# Needs contentfit/fixtures (python3 -I tools/make_contentfit_fixtures.py).

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
CC=${CC:-gcc}
T=$(mktemp -d /tmp/contentfit-host-test.XXXXXX) || exit 1
FAILED=0

cleanup() {
    status=$?
    trap - EXIT HUP INT TERM
    rm -rf -- "$T" || true
    if [ "$status" -ne 0 ]; then
        echo "contentfit host tests FAILED (exit status $status)" >&2
    fi
    exit "$status"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

fail() { echo "FAIL: $*"; FAILED=1; }

FIX="$ROOT/contentfit/fixtures"
[ -f "$FIX/cf_fixtures.h" ] || { echo "fixtures missing: run python3 -I tools/make_contentfit_fixtures.py" >&2; exit 2; }
python3 -I "$ROOT/tools/check_contentfit_fixtures.py" --fixture-dir "$FIX" > "$T/fixcheck.txt" 2>&1 || {
    cat "$T/fixcheck.txt"; echo "fixture check failed" >&2; exit 3; }
echo "fixture check: $(tail -1 "$T/fixcheck.txt")"

mkdir -p "$T/zinc"
cp /usr/local/vitasdk/arm-vita-eabi/include/zlib.h /usr/local/vitasdk/arm-vita-eabi/include/zconf.h "$T/zinc/" || exit 4
ZLIB=/usr/lib/x86_64-linux-gnu/libz.so.1
TH="$ROOT/tests/host/contentfit"
CF="$ROOT/contentfit/src"
INC="-I$TH/stub -I$TH -I$CF -I$FIX -I$ROOT/gdatproto/src -I$ROOT/resproto/src -I$ROOT/src -I$T/zinc"
CF_SRCS="cf_common cf_fixinfo cf_stats cf_budget cf_paths cf_zinflate cf_report cf_geom cf_scene cf_pack cf_suite cf_scenes cf_io cf_load cf_memtest"
OTHER="$ROOT/gdatproto/src/gp_common.c $ROOT/gdatproto/src/gp_gdat.c $ROOT/gdatproto/src/gp_wrap.c \
$ROOT/resproto/src/rp_io.c $ROOT/resproto/src/rp_inflate.c $ROOT/src/graphics/hud2d.c \
$TH/cf_gl_stub.c $TH/cf_plat_host.c"

echo "### 1. warnings-as-errors compiles"
for opt in -O1 -O2 -O3; do
    for f in $CF_SRCS; do
        $CC -std=gnu99 $opt -Wall -Wextra -Werror -ffp-contract=off $INC -c "$CF/$f.c" -o "$T/w.o" 2> "$T/w.txt" ||
            { cat "$T/w.txt"; fail "$opt $f.c"; }
    done
    $CC -std=gnu99 $opt -Wall -Wextra -Werror $INC -c "$CF/cf_iowrap.c" -o "$T/w.o" 2> "$T/w.txt" ||
        { cat "$T/w.txt"; fail "$opt cf_iowrap.c"; }
    for f in cf_gl_stub cf_plat_host test_cf_units test_cf_scenes test_cf_dryrun test_cf_iowrap fake_vgl; do
        $CC -std=gnu99 $opt -Wall -Wextra -Werror $INC -c "$TH/$f.c" -o "$T/w.o" 2> "$T/w.txt" ||
            { cat "$T/w.txt"; fail "$opt $f.c"; }
    done
    echo "    $opt done"
done

# build NAME TEST_SOURCE FLAGS...  (rp_mem.c uses mallinfo, deprecated in glibc only)
build() {
    name=$1; src=$2; shift 2
    objs=""
    for f in $CF_SRCS; do
        $CC -std=gnu99 -O1 -g -ffp-contract=off -fno-omit-frame-pointer "$@" $INC -c "$CF/$f.c" -o "$T/$name-$f.o" || return 1
        objs="$objs $T/$name-$f.o"
    done
    $CC -std=gnu99 -O1 -g -Wno-deprecated-declarations -fno-omit-frame-pointer "$@" $INC \
        -c "$ROOT/resproto/src/rp_mem.c" -o "$T/$name-rp_mem.o" || return 1
    $CC -std=gnu99 -O1 -g -fno-omit-frame-pointer "$@" $INC "$TH/$src" $OTHER $objs "$T/$name-rp_mem.o" \
        "$ZLIB" -lpthread -lm -o "$T/$name"
}

SAN="-fsanitize=address,undefined"
echo "### 2-5. sanitizer builds (mandatory)"
for b in "units test_cf_units.c" "scenes test_cf_scenes.c" "dryrun test_cf_dryrun.c"; do
    set -- $b
    build "$1" "$2" $SAN || { status=$?; echo "sanitizer build of $1 FAILED (exit status $status)" >&2; exit 5; }
    echo "    $1 (ASan+UBSan) built"
done
build units_tsan test_cf_units.c -fsanitize=thread || { echo "ThreadSanitizer build FAILED" >&2; exit 6; }
echo "    units (TSan) built"
# shader-cache redirect: the unmodified cf_iowrap.c linked with --wrap, as on the Vita
$CC -std=gnu99 -O1 -g -fno-omit-frame-pointer $SAN $INC -Wl,--wrap=sceIoOpen,--wrap=sceIoMkdir \
    "$TH/test_cf_iowrap.c" "$TH/fake_vgl.c" "$CF/cf_iowrap.c" "$CF/cf_paths.c" "$CF/cf_common.c" \
    "$ROOT/resproto/src/rp_io.c" "$ROOT/gdatproto/src/gp_common.c" "$T/units-rp_mem.o" "$ZLIB" -o "$T/iowrap" ||
    { echo "sanitizer build of iowrap FAILED" >&2; exit 7; }
echo "    iowrap (ASan+UBSan, --wrap) built"

W="$T/work"
mkdir -p "$W/ux0:data"
ln -s "$FIX" "$W/app0:fixtures"
export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
export TSAN_OPTIONS=halt_on_error=1

run() {   # label, command...
    label=$1; shift
    ( cd "$W" && "$@" ) > "$T/$label.txt" 2>&1
    st=$?
    diag=$(grep -c -E 'ERROR: (AddressSanitizer|LeakSanitizer)|runtime error:|WARNING: ThreadSanitizer' "$T/$label.txt")
    tail -3 "$T/$label.txt" | sed 's/^/    /'
    echo "    $label: exit $st, sanitizer diagnostics $diag"
    if [ "$st" -ne 0 ] || [ "$diag" -ne 0 ]; then
        grep -E 'FAIL|NOT OK|ERROR|runtime error|WARNING: ThreadSanitizer' "$T/$label.txt" | head -20
        fail "$label"
    fi
}

echo "### 2. unit tests (ASan+UBSan)"
run units "$T/units" "$T/stats_selftest.txt"
python3 -I "$ROOT/tools/analyze_contentfit_results.py" --stats-selftest "$T/stats_selftest.txt" || fail "stats self-test"
echo "### 3. unit tests (ThreadSanitizer)"
run units_tsan "$T/units_tsan" "$T/stats_selftest_tsan.txt"
echo "### 3b. shader-cache redirect, linked with --wrap (ASan+UBSan)"
run iowrap "$T/iowrap"
echo "### 4. scene check (ASan+UBSan)"
run scenes "$T/scenes"
sed -n '/^scene /,/^C3-T-PVRTC4/p' "$T/scenes.txt" | sed 's/^/    /'

echo "### 5. whole-suite dry runs (ASan+UBSan); host timings are not measurements"
first_hash=""
n=0
for mode in full quick-vblank quick-heap-unknown quick-tight-heap abort; do
    n=$((n + 1))
    run "dryrun-$mode" "$T/dryrun" "$mode"
    csv=$(printf '%s/ux0:data/ContentFit/run%03d.csv' "$W" "$n")
    if [ ! -f "$csv" ]; then
        fail "run $n files missing"
        continue
    fi
    [ -n "$first_hash" ] || first_hash=$(sha256sum "$W/ux0:data/ContentFit/run001.csv" | cut -c1-64)
    python3 -I "$ROOT/tools/analyze_contentfit_results.py" "$csv" > "$T/analyze-$mode.txt" 2>&1 ||
        { tail -20 "$T/analyze-$mode.txt"; fail "analyzer on $mode"; }
    echo "    analyzer: $(tail -1 "$T/analyze-$mode.txt")"
done
now_hash=$(sha256sum "$W/ux0:data/ContentFit/run001.csv" | cut -c1-64)
[ "$first_hash" = "$now_hash" ] || fail "run001.csv changed after later runs"
echo "    run files: $(ls "$W/ux0:data/ContentFit" | tr '\n' ' ')"
echo "    run001.csv unchanged after four later runs: $([ "$first_hash" = "$now_hash" ] && echo yes || echo NO)"
cp "$T/analyze-full.txt" "${CF_HOST_REPORT_DIR:-$T}/contentfit_dryrun_full_analysis.txt" 2>/dev/null || true
cp "$T/scenes.txt" "${CF_HOST_REPORT_DIR:-$T}/contentfit_scene_check.txt" 2>/dev/null || true

if [ "$FAILED" -ne 0 ]; then
    echo "contentfit host tests: FAILURES above"
    exit 1
fi
echo "all contentfit host tests passed"
exit 0
