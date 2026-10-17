#!/usr/bin/env bash
# Native engine tests (Linux host). Windows: compile test_ws_win.c with clang for
# x86_64-pc-windows-msvc (see ../build/build-native.sh flags) and run next to aquaws.dll.
# arm64: build test_ws_static.c with the engine sources as a static binary and run under qemu.
set -euo pipefail
cd "$(dirname "$0")"
LIB=$(realpath "${1:-../linux/libaquaws.so}")
cc -O1 -o test_ws_self test_ws_self.c -ldl
./test_ws_self "$LIB" && echo "WEBSOCKET_NATIVE_OK"
rm -f test_ws_self
