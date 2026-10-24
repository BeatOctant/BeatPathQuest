#!/usr/bin/env bash
# Build the .so and the .qmod. Needs qpm, cmake and ninja on PATH and ndkpath.txt next to qpm.json.
set -euo pipefail
cd "$(dirname "$0")/.."

[ -f ndkpath.txt ] || { echo "ndkpath.txt がありません（NDK 27 の場所を 1 行で書く）" >&2; exit 1; }

qpm restore
cmake -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -B build
cmake --build build
# qmod zip が拾えるよう、同梱ライセンスの表示（mod.json の fileCopies）を qmodIncludeDirs の build/ に置く
cp NOTICE.txt build/NOTICE.txt
qpm qmod manifest
qpm qmod zip --skip_build --offline
