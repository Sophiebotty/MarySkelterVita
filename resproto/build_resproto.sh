#!/bin/bash
# Build Vita ResProto (run from WSL). Output: <project>/build-resproto/VitaResProto.vpk
# (excluded by .gitignore via /build-*/). The main app's build/ is never used.
set -euo pipefail

export VITASDK="${VITASDK:-/usr/local/vitasdk}"
export PATH="$VITASDK/bin:$PATH"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build-resproto"

# PC-side fixtures (independent zlib + VRP1 writer). Regenerated if missing.
if [ ! -f "$ROOT/resproto/fixtures/manifest.txt" ]; then
  python3 -I "$ROOT/tools/make_resproto_fixtures.py"
fi

cmake -S "$ROOT/resproto" -B "$OUT" -DCMAKE_BUILD_TYPE=Release
cmake --build "$OUT" -j"$(nproc)"

echo
echo "Output: $OUT/VitaResProto.vpk  (title VDNG00003)"
