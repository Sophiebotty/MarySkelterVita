#!/usr/bin/env bash
# Stage 10.2 checks of the Vita ResPipe fixtures (PC only, synthetic data):
#  1. generate respipe/fixtures/ (the generator runs the independent checker
#     on its staging copy before moving anything into place);
#  2. generate again into a temporary folder: every file must be byte-identical;
#  3. run the independent checker on respipe/fixtures/;
#  4. sanity: the checker's pipeline must accept three valid packs and reject
#     deliberately corrupted copies of them;
#  5. compile rs_fixtures.h on the PC (syntax only, warnings are errors);
#  6. git: no tracked file changed, and the fixtures are ignored.
# Run from WSL: bash tests/host/run_respipe_fixture_tests.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
PY=(python3 -I -B)
GEN=tools/make_respipe_fixtures.py
CHK=tools/check_respipe_fixtures.py
FIX=respipe/fixtures
mkdir -p respipe
TMP="$(mktemp -d "$ROOT/respipe/.respipe-fixture-test-XXXXXX")"
trap 'rm -rf -- "$TMP"' EXIT
fail() { echo "FAIL: $*"; exit 1; }

echo "== 1. generate $FIX"
"${PY[@]}" "$GEN"

echo "== 2. regenerate and compare"
"${PY[@]}" "$GEN" --output-dir "$TMP/again" > "$TMP/again.log"
(cd "$FIX" && find . -type f -print0 | sort -z | xargs -0 sha256sum) > "$TMP/first.sha256"
(cd "$TMP/again" && find . -type f -print0 | sort -z | xargs -0 sha256sum) > "$TMP/second.sha256"
cmp -s "$TMP/first.sha256" "$TMP/second.sha256" || { diff "$TMP/first.sha256" "$TMP/second.sha256" || true; fail "regeneration differs"; }
echo "byte-identical: $(wc -l < "$TMP/first.sha256") files"

echo "== 3. independent checker"
"${PY[@]}" "$CHK" --fixture-dir "$FIX"

echo "== 4. sanity: corrupted copies must be rejected"
"${PY[@]}" - "$FIX" "$TMP" <<'EOF'
import struct, sys
fix, tmp = sys.argv[1], sys.argv[2]

def regions(d):
    return [struct.unpack_from("<II", d, 8 + 8 * i) for i in range(struct.unpack_from("<I", d, 4)[0])]

d = bytearray(open(fix + "/room.gdat", "rb").read())
off, size = regions(d)[5]                      # the mesh: one byte inverted mid-stream
d[off + 0x90 + (size - 0x90) // 2] ^= 0xFF
open(tmp + "/s1-room-stream.gdat", "wb").write(d)
d = open(fix + "/fmt_dxt1.gdat", "rb").read()  # last byte removed
open(tmp + "/s2-dxt1-short.gdat", "wb").write(d[:-1])
d = bytearray(open(fix + "/area8.gdat", "rb").read())
off, size = regions(d)[-1]                     # a wrapper header byte set
d[off + 0x40] = 1
open(tmp + "/s3-area8-wrapper.gdat", "wb").write(d)
EOF
for f in room fmt_dxt1 area8; do
    out="$("${PY[@]}" "$CHK" --evaluate "$FIX/$f.gdat")" || fail "$f.gdat not accepted: $out"
    echo "$f.gdat (original): $out"
done
for s in s1-room-stream s2-dxt1-short s3-area8-wrapper; do
    set +e
    out="$("${PY[@]}" "$CHK" --evaluate "$TMP/$s.gdat")"
    rc=$?
    set -e
    echo "$s: $out"
    [ "$rc" -eq 2 ] || fail "corrupted copy $s not rejected (exit $rc)"
done

echo "== 5. rs_fixtures.h compiles"
cat > "$TMP/header_check.c" <<'EOF'
#include "rs_fixtures.h"
int main(void)
{
    return RS_FIX_PACKS[0].file_size == 0u || RS_FIX_CASES[0].expect_code == 0 ||
           RS_FIX_LEVEL_RGBA[0][0][0] == 0u;
}
EOF
gcc -std=c99 -pedantic -Wall -Wextra -Werror -fsyntax-only -I "$FIX" "$TMP/header_check.c"
echo "rs_fixtures.h: no warnings"

echo "== 6. git"
git diff --quiet HEAD -- || { git diff --stat HEAD; fail "tracked files changed"; }
for f in "$FIX/room.gdat" "$FIX/bad/I-01.gdat" "$FIX/rs_fixtures.h" "$FIX/expected.json"; do
    git check-ignore -q "$f" || fail "$f is not ignored"
done
[ -z "$(git status --porcelain --untracked-files=all -- "$FIX")" ] || fail "fixtures visible to git"
echo "no tracked file changed; fixtures ignored"

echo "ALL RESPIPE FIXTURE TESTS PASSED"
