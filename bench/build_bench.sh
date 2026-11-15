#!/bin/bash
# Build VitaBench (run from WSL). Output goes to <project>/build-bench-msaaN/,
# which .gitignore excludes (/build-*/). The main app's build/ is never used.
#
#   bash bench/build_bench.sh            # MSAA 0
#   VITA_MSAA=2 bash bench/build_bench.sh
#   VITA_MSAA=4 bash bench/build_bench.sh
set -euo pipefail

export VITASDK="${VITASDK:-/usr/local/vitasdk}"
export PATH="$VITASDK/bin:$PATH"

MSAA="${VITA_MSAA:-0}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build-bench-msaa$MSAA"

cmake -S "$ROOT/bench" -B "$OUT" -DCMAKE_BUILD_TYPE=Release -DVITA_MSAA="$MSAA"
cmake --build "$OUT" -j"$(nproc)"

echo
echo "Output: $OUT/VitaBench.vpk  (title VDNG00002, MSAA $MSAA)"
