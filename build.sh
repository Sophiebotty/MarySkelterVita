#!/bin/bash
# Configure and build the Vita VPK. Run from WSL (Ubuntu), in the project root.
set -euo pipefail

export VITASDK="${VITASDK:-/usr/local/vitasdk}"
export PATH="$VITASDK/bin:$PATH"

cd "$(dirname "$0")"

# Regenerate placeholder LiveArea art if missing.
if [ ! -f sce_sys/icon0.png ]; then
  python3 -I tools/make_placeholder_art.py
fi

# MSAA: VITA_MSAA=2 ./build.sh  (0 = off [default], 2 or 4)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DVITA_MSAA="${VITA_MSAA:-0}"
cmake --build build -j"$(nproc)"

echo
echo "Output: build/VitaDungeonTest.vpk"
