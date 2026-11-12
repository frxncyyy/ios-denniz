#!/bin/bash
# SPDX-License-Identifier: MIT
# builds n2mnld for iOS 16 arm64 (freestanding like gpu/hwcodec/locbridge; ad-hoc signed by ld64.lld).
#   build_ios.sh [LIBMNL.so]  -> build/n2mnld, "N2MNLD BUILD OK <sha256>"
# Checks: libmnl sha, layout (__MNLD - n2mnl_base == 0x576000, 16 KiB aligned), r-x/rw segment protections, every
# undefined symbol covered by libSystem-n2mnld.tbd, every libmnl import has a shim (n2mnld refuses to start otherwise).
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"; OUT=$H/build; mkdir -p "$OUT"
LIB=${1:-$H/../../../../build/cam-vendor-20261109/vfs/lib64/mt6893/libmnl.so}
echo "9a1e1be78ef7d4f1f8981c9486584e47e3442637bcb9beeb8868c5929d711587  $LIB" | sha256sum -c --quiet
python3 -I "$H/mkimage.py" "$LIB" "$OUT"
F=(--target=arm64-apple-ios16.0 -std=c11 -O2 -fno-common -ffreestanding -nostdinc -isystem "$(clang -print-resource-dir)/include" -fno-builtin -Wall -Wextra -Wno-unused-parameter -Werror)
clang -c "${F[@]}" -o "$OUT/n2shim.o" n2shim.c
clang -c "${F[@]}" -o "$OUT/n2mnld.o" n2mnld.c
clang -c "${F[@]}" -o "$OUT/n2trace.o" n2trace.c
clang -c --target=arm64-apple-ios16.0 -o "$OUT/n2shim_va.o" n2shim_va.S
clang -c --target=arm64-apple-ios16.0 -o "$OUT/n2mnl_image.o" n2mnl_image.S
ld64.lld -arch arm64 -platform_version ios 16.0 17.0 -adhoc_codesign -segprot __MNLT rx rx -o "$OUT/n2mnld" \
	"$OUT/n2mnld.o" "$OUT/n2shim.o" "$OUT/n2trace.o" "$OUT/n2shim_va.o" "$OUT/n2mnl_image.o" "$H/libSystem-n2mnld.tbd"
for u in $(llvm-nm -u "$OUT/n2mnld" | awk '{print $NF}'); do grep -q -- "- $u\$" "$H/libSystem-n2mnld.tbd" || { echo "uncovered $u" >&2; exit 1; }; done
B=$(llvm-nm "$OUT/n2mnld" | awk '$3=="_n2mnl_base"{print $1}'); D=$(llvm-nm "$OUT/n2mnld" | awk '$3=="_n2mnl_data"{print $1}')
python3 -c "import sys;b,d=int(sys.argv[1],16),int(sys.argv[2],16);assert d-b==0x576000 and d%0x4000==0,(hex(b),hex(d));print('layout ok base %#x data %#x'%(b,d))" "$B" "$D"
llvm-objdump --macho --private-headers "$OUT/n2mnld" | tr -s ' ' | grep -A6 'segname __MNL' | grep -E 'segname|initprot' | paste - - | tee /dev/stderr | grep -q '__MNLT.*r-x' 
python3 - "$H/build/mnl_tab.h" "$H/n2shim.c" <<'PY'
import re,sys
imps=re.findall(r'^\t"([^"]+)",',open(sys.argv[1]).read(),re.M); src=open(sys.argv[2]).read()
have=set(re.findall(r'\b[DSV]\((\w+)\)',src.split(' tab[] = {')[1]))
miss=[i for i in imps if i not in have]; assert not miss, miss; print('shim table covers all %d imports'%len(imps))
PY
file "$OUT/n2mnld" | grep -q 'Mach-O 64-bit arm64 executable'
S=$(sha256sum "$OUT/n2mnld" | cut -d' ' -f1); ls -l "$OUT/n2mnld"; echo "N2MNLD BUILD OK $S"
