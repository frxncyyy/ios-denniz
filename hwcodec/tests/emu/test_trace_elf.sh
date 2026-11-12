#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Real-loader symbolisation test: a tiny ARM .so is loaded through V03, the trace hook is
# called at the symbol address and the dump must name "libfixture.so:trace_symbol+0x0".
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
tmp="${N2HW_TMP:-/tmp/n2hw-T01}/trace-elf"
mkdir -p "$tmp"

cat > "$tmp/fixture.c" <<'EOF'
int trace_symbol(int x) { return x + 1; }
EOF
clang --target=armv7a-linux-gnueabi -ffreestanding -fno-builtin -nostdlib -fPIC -shared \
	-fuse-ld=lld -fno-stack-protector -fno-unwind-tables -Wl,--hash-style=gnu \
	"$tmp/fixture.c" -o "$tmp/libfixture.so"

clang -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
	-fno-sanitize-recover=all -I "$root/include" -I "$here" \
	"$root/loader/n2elf.c" "$root/loader/n2elf_reloc.c" \
	"$root/loader/test_v03/stub/n2a32_stub.c" \
	"$root/emu/n2a32_trace.c" "$here/t_trace_elf.c" -o "$tmp/t_trace_elf"
"$tmp/t_trace_elf" "$tmp/libfixture.so"
