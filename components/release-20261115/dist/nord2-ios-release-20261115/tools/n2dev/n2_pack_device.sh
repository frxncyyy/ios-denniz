#!/bin/bash
# n2_pack_device.sh OUT.img KERNEL.dev DEVICE.adt BASE.img   --  boot image for THIS phone (host only, no device access)
#   KERNEL.dev : release kernel template after n2_kernel_pins.py apply (refused if any placeholder is left)
#   DEVICE.adt : mk_device_adt.py output (this phone's Wi-Fi calibration + MAC)
#   BASE.img   : the rescue/base boot image YOU built with nord2-mkrescue.py from your own stock boot+recovery and YOUR adb key
# Uses the release's host tools (HT=dir with mkpayload.py adt.py nord2-pack-xnu-nostock.py nord2-check-xnu-nostock.py loader-Image
# stub-Image, release-manifest.json) and zopfli (ZOPFLI=path).  Same steps as the owner's pack_with_args_nosyms.py (nostock layout).
set -euo pipefail
OUT=${1:?OUT.img}; K=${2:?KERNEL.dev}; ADT=${3:?DEVICE.adt}; BASE=${4:?BASE.img}
H=$(cd "$(dirname "$0")" && pwd); HT=${HT:-$H/../host-tools}; Z=${ZOPFLI:-zopfli}; M=${MANIFEST:-$H/../release-manifest.json}
[ ! -e "$OUT" ] || { echo "$OUT exists"; exit 2; }
python3 -I "$H/n2_kernel_pins.py" check "$K" --map "${PINS_MAP:-$H/../kernel/pins-map.json}" || { echo "REFUSED: placeholders left in the kernel"; exit 3; }
python3 -I -c "
import sys,os; sys.path.insert(0,'$H'); import n2adt
r=n2adt.parse_all(open('$ADT','rb').read()); c=n2adt.child(r,'chosen')
assert c.prop('nord2-wifi-calibration') is not None and c.prop('mac-address-wifi0') is not None" || { echo "REFUSED: ADT lacks this phone's Wi-Fi data"; exit 4; }
CMD=$(python3 -I -c "import json;print(json.load(open('$M'))['commandline'])")
PB=$(python3 -I -c "import json;print(json.load(open('$M'))['phys_base'])"); MS=$(python3 -I -c "import json;print(json.load(open('$M'))['mem_size'])")
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
export NORD2_PHYS_BASE=$PB NORD2_MEM_SIZE=$MS
( cd "$HT" && python3 mkpayload.py "$K" "$ADT" "$T/gpu.payload" --cmdline "$CMD" ) > "$T/payload.log"
( cd "$HT" && python3 nord2-pack-xnu-nostock.py "$BASE" "$HT/loader-Image" "$T/gpu.payload" "$OUT" --zopfli "$Z" --stub "$HT/stub-Image" ) > "$T/pack.log"
( cd "$HT" && python3 nord2-check-xnu-nostock.py "$OUT" "$T/gpu.payload" --stock "$BASE" --stub "$HT/stub-Image" --loader "$HT/loader-Image" ) > "$T/check.log" 2>&1 || true
grep -q 'FAILURES: none' "$T/check.log" || { cat "$T/check.log" | tail -20; rm -f "$OUT"; echo "PACK_REFUSED: image check failed"; exit 5; }
S=$(stat -c %s "$OUT"); [ "$S" -le 33554432 ] || { rm -f "$OUT"; echo "image too big for the boot partition"; exit 6; }
chmod 600 "$OUT"; echo "PACK_OK $OUT $S bytes sha256 $(sha256sum "$OUT" | cut -c1-64) (private: carries this phone's Wi-Fi data)"
