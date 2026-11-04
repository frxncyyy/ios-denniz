#!/bin/sh
# Native arm64 iOS bundle ABI/link test for the source-only Metal shim.
#
# This host has no iPhoneOS SDK and no Metal/Foundation headers.  The test
# therefore uses the Clang builtin freestanding headers plus minimal libc
# declarations, compiles the real shim/core, and links a Mach-O bundle with
# -undefined dynamic_lookup.  It validates target/arch/ABI and that the source
# links; it is NOT an installable Metal.framework plugin and is never run.
set -eu
cd "$(dirname "$0")/.."
WORK=$PWD
OUT=$WORK/build/ios-bundle
BUNDLE=$OUT/Nord2Metal.bundle
INC=$OUT/include
rm -rf "$OUT"
mkdir -p "$BUNDLE" "$INC"

cat > "$INC/string.h" <<'EOF'
#pragma once
#include <stddef.h>
void *memcpy(void *, const void *, size_t);
void *memset(void *, int, size_t);
int memcmp(const void *, const void *, size_t);
EOF
cat > "$INC/stdlib.h" <<'EOF'
#pragma once
#include <stddef.h>
void *malloc(size_t);
void free(void *);
void abort(void) __attribute__((noreturn));
EOF
cat > "$INC/stdio.h" <<'EOF'
#pragma once
#include <stddef.h>
int printf(const char *, ...);
int snprintf(char *, size_t, const char *, ...);
EOF
cat > "$INC/assert.h" <<'EOF'
#pragma once
#define static_assert _Static_assert
#define assert(x) ((void)0)
EOF

RES=$(clang -print-resource-dir)
CFLAGS="-std=c11 -O2 -fblocks --target=arm64-apple-ios16.0 -ffreestanding \
  -nostdinc -isystem $RES/include -isystem $INC -fno-objc-arc -Imetal"
# shellcheck disable=SC2086
clang -c $CFLAGS metal/nord2_metal.c -o "$OUT/nord2_metal.o"
# shellcheck disable=SC2086
clang -c $CFLAGS -Wno-objc-root-class metal/Nord2Metal.m \
  -o "$OUT/Nord2Metal.o"

ld64.lld -bundle -arch arm64 -platform_version ios 16.0 16.0 \
  -undefined dynamic_lookup -o "$BUNDLE/Nord2Metal" \
  "$OUT/nord2_metal.o" "$OUT/Nord2Metal.o"

cat > "$BUNDLE/Info.plist" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleExecutable</key><string>Nord2Metal</string>
<key>CFBundleIdentifier</key><string>local.nord2.metal.stub</string>
<key>CFBundlePackageType</key><string>BNDL</string>
<key>CFBundleVersion</key><string>1</string>
<key>CFBundleShortVersionString</key><string>0.0.0-stub</string>
</dict></plist>
EOF

file "$BUNDLE/Nord2Metal" | tee "$OUT/file.txt"
test "$(file -b "$BUNDLE/Nord2Metal")" = \
  "Mach-O 64-bit arm64 bundle, flags:<NOUNDEFS|DYLDLINK|TWOLEVEL>"
if command -v "${LLVM_NM:-llvm-nm}" >/dev/null 2>&1; then
  NM=${LLVM_NM:-llvm-nm}
  "$NM" -g "$BUNDLE/Nord2Metal" | grep -q '_n2m_device_init'
  "$NM" -g "$BUNDLE/Nord2Metal" | grep -q 'OBJC_CLASS_$_Nord2MetalDevice'
fi
echo "iOS arm64 bundle ABI/link stub PASS (not installable; no SDK/frameworks)"
