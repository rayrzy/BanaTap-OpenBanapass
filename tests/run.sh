#!/bin/bash
# Build src/banapass.cpp against the Win32 emulation and run the logic tests.
# Linux only. Works in a temporary directory, so nothing is left behind.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

g++ -std=c++11 -pthread -Wall -Wno-unused-parameter -Wno-unused-function \
    -I"$root/tests/stubs" -I"$root/src" \
    "$root/tests/test_banapass.cpp" "$root/tests/win32_emulation.cpp" "$root/src/banapass.cpp" \
    -o "$tmp/test_banapass"

cd "$tmp"
./test_banapass
