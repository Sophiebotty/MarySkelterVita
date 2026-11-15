#!/bin/bash
# Build Vita GDAT Proto (run from WSL). Output: <project>/build-gdatproto/VitaGdatProto.vpk
# (excluded by .gitignore via /build-*/). Other build directories are never used.
set -euo pipefail

export VITASDK="${VITASDK:-/usr/local/vitasdk}"
export PATH="$VITASDK/bin:$PATH"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build-gdatproto"

# Synthetic fixtures + expected results (PC side). Regenerated if missing,
# then always checked by the separate Python reader before packaging.
if [ ! -f "$ROOT/gdatproto/fixtures/expected.txt" ]; then
  python3 -I "$ROOT/tools/make_gdatproto_fixtures.py"
fi
python3 -I "$ROOT/tools/check_gdatproto_fixtures.py"

cmake -S "$ROOT/gdatproto" -B "$OUT" -DCMAKE_BUILD_TYPE=Release
cmake --build "$OUT" -j"$(nproc)"

echo
echo "Output: $OUT/VitaGdatProto.vpk  (title VDNG00004)"
