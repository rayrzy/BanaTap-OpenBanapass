#!/bin/bash
# Syntax-check every source file against the stand-in headers in tests/stubs.
set -euo pipefail
cd "$(dirname "$0")/.."

for f in src/banapass.cpp src/dllmain_*.cpp; do
    echo "checking $f"
    g++ -std=c++11 -fsyntax-only -Wall -Wextra -Wno-unused-parameter \
        -Itests/stubs -Isrc '-DBANATAP_VERSION="ci"' "$f"
done
echo "all sources passed the syntax check"
