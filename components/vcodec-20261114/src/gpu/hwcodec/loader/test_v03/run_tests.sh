#!/bin/bash
# SPDX-License-Identifier: MIT
# V03 host tests. Builds under /tmp/n2hw-V03 only; no phone, no network.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
hwd=$(cd "$here/../.." && pwd)
out=/tmp/n2hw-V03
fix=$out/fix
CC=${CC:-clang}
TARGET="--target=armv7a-linux-androideabi"
ARM_COMMON="-fPIC -shared -nostdlib -fuse-ld=lld -O1"
SAN="-std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all"
OPT="-std=c11 -O2 -g -Wall -Wextra -Werror"

rm -rf "$out/build" "$fix"
mkdir -p "$out/build" "$fix"

fail() {
	trap - ERR
	echo "N2TEST V03 FAIL $1"
	exit 1
}
trap 'echo "N2TEST V03 FAIL unexpected command failure (line $LINENO)"; exit 1' ERR

arm() { nice -n 10 "$CC" $TARGET $ARM_COMMON "$@"; }

# ---- small ARM shared objects: plain REL, Android packed, Android packed + RELR, RELR, Thumb ----
arm -Wl,-soname,liba.so "$here/fixtures/liba.c" -o "$fix/liba.so"
arm -mthumb -Wl,--pack-dyn-relocs=android -Wl,-soname,libb.so \
	"$here/fixtures/libb.c" -L"$fix" -l:liba.so -o "$fix/libb.so"
arm -Wl,--pack-dyn-relocs=android+relr -Wl,-soname,libc.so \
	"$here/fixtures/libc.c" -L"$fix" -l:libb.so -l:liba.so -o "$fix/libc.so"
arm -Wl,--pack-dyn-relocs=relr -Wl,-soname,libd.so "$here/fixtures/libd.c" -o "$fix/libd.so"
# ARM-mode object built against liba (GOT/PLT through the real interpreter in t_real; also oracle-checked)
arm -Wl,-soname,libarm.so "$here/fixtures/libarm.c" -L"$fix" -l:liba.so -o "$fix/libarm.so"
arm -Wl,-soname,provider.so "$here/fixtures/provider.c" -o "$fix/provider.so"
arm -Wl,-soname,libbad.so "$here/fixtures/baddata.c" -L"$fix" -l:provider.so -o "$fix/libbad.so"
nice -n 10 "$CC" $TARGET -nostdlib -fuse-ld=lld -Wl,--no-pie -Wl,-e,_start \
	"$here/fixtures/exec.c" -o "$fix/exec.elf"

# ---- host binaries: sanitizer configuration and -O2 configuration ----
nice -n 10 "$CC" $SAN -I "$hwd/include" \
	"$hwd/loader/n2elf.c" "$hwd/loader/n2elf_reloc.c" \
	"$here/stub/n2a32_stub.c" "$here/t_v03.c" -o "$out/build/t_v03"
nice -n 10 "$CC" $OPT -I "$hwd/include" \
	"$hwd/loader/n2elf.c" "$hwd/loader/n2elf_reloc.c" \
	"$here/stub/n2a32_stub.c" "$here/t_v03.c" -o "$out/build/t_v03_o2"

# ---- loader against V02's real interpreter (V02 DONE at 12:24) ----
REAL_SRCS="$hwd/loader/n2elf.c $hwd/loader/n2elf_reloc.c
	$hwd/emu/n2a32_core.c $hwd/emu/n2a32_mem.c $hwd/emu/n2a32_excl.c
	$hwd/emu/n2a32_arm_dp.c $hwd/emu/n2a32_arm_mem.c $hwd/emu/n2a32_arm_misc.c
	$hwd/emu/n2a32_arm_media.c $hwd/emu/n2a32_t16.c $hwd/emu/n2a32_stub_classes.c"
nice -n 10 "$CC" $SAN -I "$hwd/include" $REAL_SRCS "$here/t_real.c" -pthread -o "$out/build/t_real"
nice -n 10 "$CC" $OPT -I "$hwd/include" $REAL_SRCS "$here/t_real.c" -pthread -o "$out/build/t_real_o2"

# ---- relocation-encoding unit tests (hand-built tables, literal expectations) ----
nice -n 10 "$CC" $SAN -I "$hwd/include" \
	"$hwd/loader/n2elf_reloc.c" "$here/t_reloc.c" -o "$out/build/t_reloc"
nice -n 10 "$CC" $OPT -I "$hwd/include" \
	"$hwd/loader/n2elf_reloc.c" "$here/t_reloc.c" -o "$out/build/t_reloc_o2"

# ---- arm64 iOS compile check (compile only; same SDK V30 uses) ----
IOS_SDK=${N2_IOS_SDK:-$hwd/../build/apple-toolchain/sdks/iPhoneOS16.5.sdk}
ios_checks=0
if [ -d "$IOS_SDK" ]; then
	for f in n2elf.c n2elf_reloc.c; do
		nice -n 10 "$CC" -target arm64-apple-ios16.5 -isysroot "$IOS_SDK" -std=c11 -O2 \
			-Wall -Wextra -Werror -I "$hwd/include" -c "$hwd/loader/$f" -o "$out/build/${f%.c}.ios.o"
	done
	ios_checks=2
	echo "V03 iOS compile: 2/2 (arm64-apple-ios16.5, compile only)"
else
	echo "V03 iOS compile: SKIP (no SDK at $IOS_SDK)"
fi

export V03_FIXTURES="$fix"

echo "== V03 oracle under ASan+UBSan"
T_V03="$out/build/t_v03" python3 "$here/oracle.py" > "$out/oracle-asan.log" 2>&1 \
	|| { cat "$out/oracle-asan.log"; fail "oracle (ASan)"; }
grep -E '^(V03 oracle|V03 vendor|V03 synthetic)' "$out/oracle-asan.log" || fail "no oracle summary"

echo "== V03 oracle under -O2 without sanitizers"
T_V03="$out/build/t_v03_o2" python3 "$here/oracle.py" > "$out/oracle-o2.log" 2>&1 \
	|| { cat "$out/oracle-o2.log"; fail "oracle (-O2)"; }
grep -E '^(V03 oracle|V03 vendor|V03 synthetic)' "$out/oracle-o2.log" || fail "no -O2 oracle summary"

echo "== V03 against V02's real interpreter (load/link/unresolved stop/init/GOT/PLT)"
"$out/build/t_real" "$fix" "$hwd/../build/cam-vendor-20261109/vfs" > "$out/real-asan.log" 2>&1 \
	|| { cat "$out/real-asan.log"; fail "t_real (ASan)"; }
"$out/build/t_real_o2" "$fix" "$hwd/../build/cam-vendor-20261109/vfs" > "$out/real-o2.log" 2>&1 \
	|| { cat "$out/real-o2.log"; fail "t_real (-O2)"; }
grep -E '^V03 real-emu:' "$out/real-asan.log"
grep -E '^V03 real-emu:' "$out/real-o2.log"

echo "== V03 relocation-encoding unit tests"
"$out/build/t_reloc" > "$out/reloc-asan.log" 2>&1 || { cat "$out/reloc-asan.log"; fail "t_reloc (ASan)"; }
"$out/build/t_reloc_o2" > "$out/reloc-o2.log" 2>&1 || { cat "$out/reloc-o2.log"; fail "t_reloc (-O2)"; }
grep '^V03 reloc unit:' "$out/reloc-asan.log"
grep '^V03 reloc unit:' "$out/reloc-o2.log"

checks=$(sed -n 's/^V03 oracle: \([0-9]*\) checks$/\1/p' "$out/oracle-asan.log")
[ -n "$checks" ] || fail "could not read the check count"
reloc_checks=$(sed -n 's/^V03 reloc unit: \([0-9]*\) checks$/\1/p' "$out/reloc-asan.log")
[ -n "$reloc_checks" ] || fail "could not read the relocation check count"
real_checks=$(sed -n 's/^V03 real-emu: \([0-9]*\) checks$/\1/p' "$out/real-asan.log")
[ -n "$real_checks" ] || fail "could not read the real-emulator check count"
checks=$((checks + reloc_checks + real_checks + ios_checks))
echo "== V03 vendor acceptance line"
grep '^V03 vendor:' "$out/oracle-asan.log"
echo "N2TEST V03 PASS $checks/$checks"
