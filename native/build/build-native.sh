#!/usr/bin/env bash
# Cross-compiles an Aquarius native library for every supported platform with
# clang + lld (no NDK, MinGW or MSVC required):
#   windows/<name>.dll                 x86_64-pc-windows-msvc, Winsock/kernel32/bcrypt only
#   linux/lib<name>.so                 x86_64-linux, raw system calls, no libc
#   android/arm64-v8a/lib<name>.so     aarch64-linux-android29, raw system calls, no libc
#   android/x86_64/lib<name>.so        x86_64-linux-android29, raw system calls, no libc
#
# Usage: build-native.sh NAME OUTPUT_DIR [extra cflags...] -- source.c ...
set -euo pipefail
NAME=$1; OUT=$2; shift 2
EXTRA=()
while [ $# -gt 0 ] && [ "$1" != "--" ]; do EXTRA+=("$1"); shift; done
[ "${1:-}" = "--" ] && shift
SOURCES=("$@")
HERE=$(cd "$(dirname "$0")" && pwd)
CLANG=${CLANG:-clang}
WINLIB=${WINLIB:-$HERE/winlib}
[ -f "$WINLIB/ws2_32.lib" ] || "$HERE/winlibs.sh" "$WINLIB" >/dev/null

COMMON=(-O2 -ffreestanding -fno-stack-protector -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables
        -ffunction-sections -fdata-sections -fvisibility=hidden -Wall -Wextra -Wno-unused-parameter "${EXTRA[@]}")
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

build_elf() { # target outdir
    local target=$1 dir=$2
    mkdir -p "$OUT/$dir"
    "$CLANG" --target="$target" "${COMMON[@]}" -fPIC -nostdlib -shared -fuse-ld=lld \
        -Wl,--gc-sections -Wl,-z,max-page-size=16384 -Wl,-z,noexecstack -Wl,--no-undefined -Wl,-soname,"lib$NAME.so" -Wl,--build-id=sha1 \
        -o "$OUT/$dir/lib$NAME.so" "${SOURCES[@]}"
    llvm-strip --strip-unneeded "$OUT/$dir/lib$NAME.so"
}
build_windows() {
    mkdir -p "$OUT/windows"
    local objs=()
    for s in "${SOURCES[@]}"; do
        local o="$TMP/$(basename "${s%.*}").obj"
        "$CLANG" --target=x86_64-pc-windows-msvc "${COMMON[@]}" -fms-extensions -c "$s" -o "$o"
        objs+=("$o")
    done
    lld-link /nologo /dll /nodefaultlib /entry:_DllMainCRTStartup /machine:x64 /opt:ref /opt:icf /release \
        /out:"$OUT/windows/$NAME.dll" "${objs[@]}" "$WINLIB/ws2_32.lib" "$WINLIB/kernel32.lib" "$WINLIB/bcrypt.lib"
    rm -f "$OUT/windows/$NAME.lib" "$OUT/windows/$NAME.exp"
}

build_elf x86_64-linux-gnu linux
build_elf aarch64-linux-android29 android/arm64-v8a
build_elf x86_64-linux-android29 android/x86_64
build_windows
ls -la "$OUT"/windows "$OUT"/linux "$OUT"/android/*
