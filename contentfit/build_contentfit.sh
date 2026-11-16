#!/bin/bash
# Build Vita ContentFit (run from WSL). Output: <project>/build-contentfit/VitaContentFit.vpk
# (excluded by .gitignore via /build-*/). Other build directories are never used.
set -euo pipefail

export VITASDK="${VITASDK:-/usr/local/vitasdk}"
export PATH="$VITASDK/bin:$PATH"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build-contentfit"

# Synthetic fixtures + expected values (PC side). Generated if missing, then
# always checked by the separate Python reader before packaging.
if [ ! -f "$ROOT/contentfit/fixtures/cf_fixtures.h" ]; then
  python3 -I "$ROOT/tools/make_contentfit_fixtures.py"
fi
python3 -I "$ROOT/tools/check_contentfit_fixtures.py"

cmake -S "$ROOT/contentfit" -B "$OUT" -DCMAKE_BUILD_TYPE=Release
cmake --build "$OUT" -j"$(nproc)"

echo
echo "Output: $OUT/VitaContentFit.vpk  (title VDNG00005)"
