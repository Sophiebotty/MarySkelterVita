#!/bin/sh
# Vita ResPipe stage 10.4 PC tests: src/resource/rs_loader.c.
# Builds and runs, in a private temporary directory:
#   1. warnings-as-errors compiles: rs_loader.c at -O0..-O3 with the strict
#      flags used for rs_format.c, and the test sources with -Wall -Wextra
#   2. rs_loader.o's undefined symbols: no GL, SDL, Vita kernel, pthread,
#      stdio or plain malloc/free call (section 7: no GL or worker work here)
#   3. test_rs_loader with AddressSanitizer + UndefinedBehaviorSanitizer
#      (-fno-sanitize-recover=all, leak checking on), linked with the
#      unmodified ContentFit reader, the PC stand-in and
#      -Wl,--wrap=rp_malloc,--wrap=rp_free; every check, every flip
#   4. the same program with ThreadSanitizer (flips sampled: every 16th)
#   5. compare_rs_results.py: the loader's packs, cases, synthetic packs and
#      flips against expected.json, RESOURCE_PIPELINE.md and the Python checker
#   6. git: no protected tracked file changed
# Both sanitizer builds are mandatory; any report fails the run.
#
# usage: sh tests/host/run_respipe_loader_tests.sh   (from WSL, project root)
# Needs respipe/fixtures (python3 -I -B tools/make_respipe_fixtures.py).

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
CC=${CC:-gcc}
T=$(mktemp -d /tmp/respipe-loader-test.XXXXXX) || exit 1
FAILED=0

cleanup() {
    status=$?
    trap - EXIT HUP INT TERM
    rm -rf -- "$T" || true
    [ "$status" -ne 0 ] && echo "respipe loader tests FAILED (exit status $status)" >&2
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

TH="$ROOT/tests/host/respipe"
INC="-I$TH -I$ROOT/src -I$ROOT/src/resource -I$ROOT/contentfit/src -I$ROOT/gdatproto/src -I$ROOT/resproto/src \
-I$FIX -I$T/zinc"
NEW="$ROOT/src/resource/rs_loader.c $ROOT/src/resource/rs_format.c"
# unmodified modules: GDAT Proto, Phase 8, ContentFit (reader, inflater, budget), room grid (policy)
REUSED="$ROOT/gdatproto/src/gp_common.c $ROOT/gdatproto/src/gp_gdat.c $ROOT/gdatproto/src/gp_wrap.c \
$ROOT/resproto/src/rp_io.c $ROOT/resproto/src/rp_inflate.c \
$ROOT/contentfit/src/cf_pack.c $ROOT/contentfit/src/cf_zinflate.c $ROOT/contentfit/src/cf_common.c \
$ROOT/contentfit/src/cf_budget.c $ROOT/src/gameplay/room_map.c"
TESTS="$TH/test_rs_loader.c $TH/rs_plat_pc.c $TH/rs_synth.c $TH/rs_host_pipe.c"
WRAP="-Wl,--wrap=rp_malloc,--wrap=rp_free"

echo "### 1. warnings-as-errors compiles"
STRICT="-Wall -Wextra -Werror -Wshadow -Wconversion -Wsign-conversion -Wcast-qual -Wstrict-prototypes"
for opt in -O0 -O1 -O2 -O3; do
    $CC -std=gnu99 $opt $STRICT $INC -c "$ROOT/src/resource/rs_loader.c" -o "$T/w.o" 2> "$T/w.txt" ||
        { cat "$T/w.txt"; fail "$opt rs_loader.c"; }
    for f in $TESTS; do
        $CC -std=gnu99 $opt -Wall -Wextra -Werror $INC -c "$f" -o "$T/w.o" 2> "$T/w.txt" ||
            { cat "$T/w.txt"; fail "$opt $(basename "$f")"; }
    done
    echo "    $opt done"
done

echo "### 2. what rs_loader.o calls"
$CC -std=gnu99 -O2 $INC -c "$ROOT/src/resource/rs_loader.c" -o "$T/rs_loader_sym.o" || fail "rs_loader.c for nm"
nm -u "$T/rs_loader_sym.o" | awk '{print $2}' | sort > "$T/undef.txt"
echo "    undefined: $(tr '\n' ' ' < "$T/undef.txt")"
if grep -E '^(gl|SDL_|sce|pthread_|malloc$|calloc$|realloc$|free$|f?open$|fread$|fwrite$|f?printf$|puts$)' \
    "$T/undef.txt"; then
    fail "rs_loader.o calls something it must not"
else
    echo "    no GL, SDL, Vita kernel, pthread, stdio or plain malloc/free"
fi

# build NAME FLAGS...
build() {
    name=$1; shift
    objs=""
    for f in $NEW $REUSED $TESTS; do
        o="$T/$name-$(basename "$f" .c).o"
        $CC -std=gnu99 -O1 -g -fno-omit-frame-pointer "$@" $INC -c "$f" -o "$o" || return 1
        objs="$objs $o"
    done
    $CC -std=gnu99 -O1 -g -fno-omit-frame-pointer -Wno-deprecated-declarations "$@" $INC \
        -c "$ROOT/resproto/src/rp_mem.c" -o "$T/$name-rp_mem.o" || return 1
    $CC "$@" $WRAP $objs "$T/$name-rp_mem.o" "$ZLIB" -lpthread -lm -o "$T/$name"
}

W="$T/work"
mkdir -p "$W/app0:fixtures"
for f in "$FIX"/*.gdat; do ln -s "$f" "$W/app0:fixtures/$(basename "$f")"; done
ln -s "$FIX/bad" "$W/app0:fixtures/bad"

run() {   # label, command...
    label=$1; shift
    start=$(date +%s)
    ( cd "$W" && "$@" ) > "$T/$label.txt" 2>&1
    st=$?
    diag=$(grep -c -E 'ERROR: (AddressSanitizer|LeakSanitizer)|runtime error:|WARNING: ThreadSanitizer|SUMMARY: (Address|Undefined|Leak|Thread)' "$T/$label.txt")
    grep -v -E '^  [ICPW]-[0-9][0-9] ' "$T/$label.txt" | sed 's/^/    /'
    echo "    $label: exit $st, sanitizer reports $diag, $(( $(date +%s) - start )) s"
    if [ "$st" -ne 0 ] || [ "$diag" -ne 0 ]; then
        grep -E 'FAIL|ERROR|runtime error|WARNING: ThreadSanitizer|SUMMARY' "$T/$label.txt" | head -40
        fail "$label"
    fi
}

echo "### 3. AddressSanitizer + UndefinedBehaviorSanitizer build and run (mandatory)"
build asan -fsanitize=address,undefined -fno-sanitize-recover=all ||
    { echo "ASan+UBSan build FAILED" >&2; exit 6; }
export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:detect_stack_use_after_return=1:strict_string_checks=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
run asan "$T/asan" "$T/results.txt"
cp "$T/asan.txt" "${RS_HOST_REPORT_DIR:-$T}/respipe_10_4_asan.txt" 2>/dev/null || true
cp "$T/results.txt" "${RS_HOST_REPORT_DIR:-$T}/respipe_10_4_results.txt" 2>/dev/null || true

echo "### 4. ThreadSanitizer build and run (mandatory; flips sampled)"
build tsan -fsanitize=thread || { echo "ThreadSanitizer build FAILED" >&2; exit 7; }
export TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1
run tsan "$T/tsan" "$T/results_tsan.txt" quick
cp "$T/tsan.txt" "${RS_HOST_REPORT_DIR:-$T}/respipe_10_4_tsan.txt" 2>/dev/null || true

echo "### 5. loader results against expected.json, RESOURCE_PIPELINE.md and the Python checker"
start=$(date +%s)
python3 -I -B "$TH/compare_rs_results.py" --results "$T/results.txt" --fixture-dir "$FIX" --work-dir "$W" \
    --sections pack,case,synth,flipf > "$T/compare.txt" 2>&1 || fail "compare_rs_results.py"
sed 's/^/    /' "$T/compare.txt" | head -60
echo "    ($(( $(date +%s) - start )) s)"

echo "### 6. git: protected files unchanged"
ALLOWED="tests/host/respipe/rs_host_pipe.c tests/host/respipe/rs_host_pipe.h tests/host/respipe/test_rs_format.c \
tests/host/respipe/compare_rs_results.py tests/host/run_respipe_host_tests.sh"
bad=""
for f in $(git -C "$ROOT" diff --name-only HEAD --); do
    case " $ALLOWED " in
        *" $f "*) ;;
        *) bad="$bad $f" ;;
    esac
done
if [ -z "$bad" ]; then
    echo "    no protected tracked file changed (prototypes, ContentFit, renderer, rs_format, tools, specification)"
else
    echo "    changed:$bad"
    fail "protected tracked files changed"
fi

if [ "$FAILED" -ne 0 ]; then
    echo "respipe loader tests: FAILURES above"
    exit 1
fi
echo "ALL RESPIPE 10.4 LOADER TESTS PASSED"
exit 0
