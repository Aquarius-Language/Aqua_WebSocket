#!/usr/bin/env bash
# Rebuilds Aqua_WebSocket's native engine for Windows, Linux and Android from native/src.
# Requirements: clang, lld, llvm-dlltool and llvm-strip (LLVM 15 or newer). No NDK, MinGW
# or MSVC is needed: the library is freestanding (raw system calls / Winsock + kernel32).
# 需求：clang、lld、llvm-dlltool、llvm-strip。輸出覆寫 native/{windows,linux,android}。
#   ./build.sh            → writes into native/
#   OUT=/tmp/x ./build.sh → writes elsewhere
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
NATIVE=$(dirname "$HERE")
SRC=$NATIVE/src
OUT=${OUT:-$NATIVE}
export WINLIB=${WINLIB:-$(mktemp -d)}
"$HERE/build-native.sh" aquaws "$OUT" -I"$SRC" -- "$SRC/aq_plat.c" "$SRC/aq_json.c" "$SRC/aquaws.c"
