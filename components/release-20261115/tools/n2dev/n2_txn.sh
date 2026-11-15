#!/bin/bash
# n2_txn.sh PAYLOAD_DIR GUEST_SCRIPT OWNED_JSON OK_MARKER KERNEL_IMG TAG
# One userland transaction on YOUR phone (generic form of the owner's txn_*.sh, same steps):
#   fastboot -> boot YOUR rescue -> snapshot of the iOS root into YOUR mirror -> offline VM runs GUEST_SCRIPT on a copy ->
#   catalog check (every changed path must be in OWNED_JSON) -> verified commit of the delta -> flash KERNEL_IMG -> USB log capture.
# Config (env or ~/.config/nord2/txn.env): ADB FASTBOOT RESCUE_IMG MIRROR (your root mirror file) HT (release host-tools dir)
#   INITRAMFS (release vm/initramfs dir)  WORK (where cc-txn-TAG/ goes)  READS (USB capture seconds, <= 600)
# It writes the phone.  Never run two at once.  Nothing here knows any phone identity.
set -euo pipefail
[ -f "${N2_TXN_ENV:-$HOME/.config/nord2/txn.env}" ] && . "${N2_TXN_ENV:-$HOME/.config/nord2/txn.env}"
P=${1:?PAYLOAD_DIR}; G=${2:?GUEST}; O=${3:?OWNED_JSON}; OK=${4:?OK_MARKER}; IMG=${5:?KERNEL_IMG}; TAG=${6:?TAG}
: "${ADB:=adb}" "${FASTBOOT:=fastboot}" "${RESCUE_IMG:?}" "${MIRROR:?}" "${HT:?}" "${INITRAMFS:?}" "${WORK:?}" "${READS:=600}"
X=$WORK/cc-txn-$TAG; [ ! -e "$X" ] || { echo "$X exists: pick a new TAG"; exit 2; }
[ -f "$P/plan.json" ] && [ -f "$G" ] && [ -f "$O" ] && [ -f "$IMG" ] || { echo "missing payload/guest/owned/image"; exit 2; }
mkdir -p "$X"; log(){ echo "$(date -Is) $*" | tee -a "$X/events"; }
fb_wait() { for i in $(seq 1 ${1:-600}); do timeout 3 $FASTBOOT devices | grep -q fastboot && return 0; sleep 1; done; return 1; }
flash() { $FASTBOOT getvar product 2>&1 | grep -q "product: oplus6893" || { echo "not a Nord 2 (oplus6893)"; exit 3; }
          [ "$(stat -c %s "$1")" -le 33554432 ] || { echo "image too big"; exit 3; }
          $FASTBOOT flash boot "$1"; $FASTBOOT reboot; }
log start "$P"; echo "put the phone in fastboot (power + volume down) if it is not already"; fb_wait 1800
flash "$RESCUE_IMG"; for i in $(seq 1 60); do sleep 3; timeout 3 $ADB devices | grep -q "recovery$" && break; done; log rescue up
python3 "$HT/snapshot_current_root_fast.py" --adb "$(command -v $ADB)" --main "$HT" --mirror "$MIRROR" --output "$X/snapshot" --jobs 4 > "$X/snapshot.log" 2>&1; log snapshot done
python3 "$HT/generic_update_offline.py" --snapshot-receipt "$X/snapshot/verified.json" --payload "$P" --guest "$G" --output "$X/offline" --initramfs "$INITRAMFS" > "$X/offline.log" 2>&1; log offline done
grep -a -q "_REFUSED\|Traceback\|AssertionError" "$X/offline/vm.log" && { log "guest refused/failed: nothing committed"; exit 3; }
grep -a -q "$OK" "$X/offline/vm.log" || { log "no $OK: nothing committed"; exit 3; }
cp "$O" "$X/owned.json"
python3 "$HT/check_update_catalog_fast.py" --hfsx-read "$HT/hfsx-read.py" --snapshot-receipt "$X/snapshot/verified.json" --update "$X/offline" --owned-json "$X/owned.json" --output "$X/catalog.json" > "$X/catalog.log" 2>&1; log catalog ok
ADDON=$(python3 -c "import json;print(json.load(open('$X/offline/verified.json'))['addon_sha256'])")
python3 "$HT/commit_root_delta_fast.py" --adb "$(command -v $ADB)" --main "$HT" --snapshot-receipt "$X/snapshot/verified.json" --update "$X/offline" --output "$X/device-install" --expected-addon-sha256 "$ADDON" --commit > "$X/device-install.log" 2>&1; log committed
timeout 8 $ADB shell 'P=/sys/devices/platform/10026000.pwrap/10026000.pwrap:mt6359-pmic/mt-pmic/pmic_access; echo 0x5b4 0x2000 > $P; echo 0x5c2 0x1 > $P; echo 1 > /proc/sys/kernel/sysrq; sync; echo b > /proc/sysrq-trigger' >/dev/null 2>&1 || true
fb_wait 300; flash "$IMG"; log kernel flashed "$(sha256sum "$IMG" | cut -c1-16)"
python3 "$HT/nord2-usb-console.py" "$X/ui-usb" --wait 240 --read-seconds "$READS" > "$X/usb.log" 2>&1 || true
log "capture done: $X/ui-usb (private: your phone's logs)"
