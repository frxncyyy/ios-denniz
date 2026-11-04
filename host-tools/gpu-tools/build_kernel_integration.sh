#!/usr/bin/env bash
# Compile the two integration members replaced in the preserved CI archive.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/kernel-native
python3 tools/xnu_staged_compile.py build/xnu-stage/iokit/KernelConfigTables.cpp --cxx \
 --out build/kernel-native/KernelConfigTables.cpo > build/kernel-native/KernelConfigTables-build.log 2>&1
python3 tools/xnu_staged_compile.py build/xnu-stage/bsd/dev/sweet_touch.c \
 --out build/kernel-native/sweet_touch.o > build/kernel-native/sweet_touch-build.log 2>&1
