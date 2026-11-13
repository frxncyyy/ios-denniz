#!/bin/sh
# Build stub-Image (the reset-to-fastboot stub that replaces the Android kernel in NORD2_BOOT_COMPRESS=nostock images).
# Same compiler recipe as tools/nord2-build-loader.sh. No device access. Refuses any data/absolute relocation.
#   build_stub.sh [OUTDIR]      (default: this directory; existing stub-Image is only replaced when the bytes are equal or FORCE=1)
set -eu
here=$(cd "$(dirname "$0")" && pwd)
out=${1:-$here}
tc=$HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/toolchains/proton-clang/bin
mkdir -p "$out/obj"
for source in head.S stub.c; do
    "$tc/clang" --target=aarch64-none-elf -O2 -ffreestanding -nostdlib -fno-pic \
        -mgeneral-regs-only -mstrict-align -fno-stack-protector -fno-builtin \
        -Wall -Wextra -Werror -c "$here/$source" -o "$out/obj/$source.o"
done
"$tc/ld.lld" -T "$here/stub.ld" --build-id=none "$out/obj/head.S.o" "$out/obj/stub.c.o" -o "$out/obj/stub.elf"
# position independence: no absolute relocations in the objects, no data sections in the link
if "$tc/llvm-readelf" -r "$out/obj/stub.c.o" "$out/obj/head.S.o" | grep -E 'R_AARCH64_ABS(64|32|16)' ; then
    echo "stub: absolute relocation (above): not position independent" >&2; exit 1
fi
if "$tc/llvm-readelf" -S "$out/obj/stub.c.o" | grep -E ' \.(data|bss)[. ]' | awk '{ if (strtonum("0x"$6) != 0 && strtonum("0x"$7) != 0) exit 1 }'; then :; else
    echo "stub: writable data in stub.c" >&2; exit 1
fi
"$tc/llvm-objcopy" -O binary "$out/obj/stub.elf" "$out/obj/stub-Image.new"
size=$(stat -c %s "$out/obj/stub-Image.new")
[ "$size" -le 16384 ] || { echo "stub: $size bytes, more than 16 KiB (stack is at +64 KiB)" >&2; exit 1; }
if [ -e "$out/stub-Image" ] && ! cmp -s "$out/obj/stub-Image.new" "$out/stub-Image" && [ "${FORCE:-0}" != 1 ]; then
    echo "stub: $out/stub-Image exists and differs (FORCE=1 replaces it)" >&2; exit 1
fi
mv "$out/obj/stub-Image.new" "$out/stub-Image"
echo "stub-Image $size bytes sha256 $(sha256sum "$out/stub-Image" | cut -c1-64)"
