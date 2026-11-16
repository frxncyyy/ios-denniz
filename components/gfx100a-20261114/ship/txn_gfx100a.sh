#!/bin/bash
# NOT EXECUTED BY ITS AUTHOR. ONE transaction of gfx100a-20261114 (copy of webxr-20261114/txn_webxr.sh): Nord2Metal gfx100a
# (= webxr1 + rtcopy snapshot write-back + ASTC edge + file log/slot diagnostics; library unchanged, must be webxr1's 9f263e6f),
# then flash KERNEL_IMG (q10jfin, unchanged). REQUIRES webxr1 already installed on the phone (the guest refuses otherwise, nothing committed).
# (rescue -> snapshot -> offline VM (journal replay + guest) -> catalog check -> verified delta commit -> flash KERNEL_IMG -> USB capture).
# It writes the phone: run it only with the user's go.
# usage (phone already in fastboot via `phone.py reset fastboot`):
#   [VARIANT=gfx100a|gfx100a-off|gfx100a-nortcopy] READS=600 txn_gfx100a.sh KERNEL_IMG TAG
#       gfx100a           the fixes + diagnostics (default)
#       gfx100a-off       the same file + /var/db/nord2-metal/no-gfx100a (all gfx100a features off, file log included = webxr1 behaviour)
#       gfx100a-nortcopy  the same file + /var/db/nord2-metal/no-rtcopy (A/B of the snapshot write-back; diagnostics on)
#   UNDO=1 txn_gfx100a.sh KERNEL_IMG TAG     back to webxr1 (record/backup Nord2Metal.pre-gfx100a; fallback copy in payload-gfx100a-undo)
#   The file logs are read back offline from /private/var/db/nord2-metal/log/ (see NOTES gfx100a: pull them BEFORE an undo, which deletes them).
set -e
trap 'echo "$(date -Is) FAILED at line $LINENO" | tee -a $X/events' ERR
W=$NORD2_WORK
B=$W/gpu/build; T=$W/gpu/tools; S=$B/gfx100a-20261114/ship; MAIN=$NORD2_REPO
A=$HOME/Android/Sdk/platform-tools/adb; D=$B/autocycle; IMG=$1; X=$B/cc-txn-$2
[ -n "$2" ] && [ -f "$IMG" ] || { echo "usage: txn_gfx100a.sh KERNEL_IMG TAG"; exit 2; }
[ ! -e "$X" ] || { echo "$X exists: pick a new TAG"; exit 2; }
READS=${READS:-600}; [ "$READS" -gt 0 ] && [ "$READS" -le 600 ] || { echo "READS must be 1..600 (nord2-usb-console.py limit)"; exit 2; }
V=${VARIANT:-gfx100a}; case $V in gfx100a|gfx100a-off|gfx100a-nortcopy) ;; *) echo "VARIANT must be gfx100a, gfx100a-off or gfx100a-nortcopy"; exit 2;; esac
P=$V; G=$S/guest-gfx100a.sh; OKRE='GFX100A_INSTALLED\|GFX100A_ALREADY'
if [ "${UNDO:-0}" = 1 ]; then P=gfx100a-undo; G=$S/guest-gfx100a-undo.sh; OKRE='GFX100A_UNDONE'; fi
O=$S/owned-gfx100a.json
mkdir -p $X
log(){ echo "$(date -Is) $*" | tee -a $X/events; }
log start $P; $D/to_fastboot.sh ${FB_WAIT:-1800}
$D/flash_boot.sh $MAIN/research/nord2/rescue/nord2-adb-rescue.img
for i in $(seq 1 40); do sleep 3; timeout 3 $A devices | grep -q "recovery$" && break; done; log rescue up
python3 $T/snapshot_current_root_fast.py --main $MAIN --mirror $B/mirror/rootfs-mirror.img --output $X/snapshot --jobs 4 > $X/snapshot.log 2>&1; log snapshot done
python3 $B/apps-20261104/generic_update_offline.py --snapshot-receipt $X/snapshot/verified.json --payload $S/payload-$P --guest $G --output $X/offline --initramfs $MAIN/research/nord2/rootfs-vm/initramfs > $X/offline.log 2>&1; log offline done
grep -a -q "AssertionError\|Traceback" $X/offline/vm.log && { log "guest refused or failed: see offline/vm.log; nothing committed"; exit 3; }
grep -a -q "$OKRE" $X/offline/vm.log || { log "no $OKRE in offline/vm.log; nothing committed"; exit 3; }
grep -a "GFX100A" $X/offline/vm.log | cut -c1-240 | tee -a $X/events
cp $O $X/owned.json
python3 $T/check_update_catalog_fast.py --hfsx-read $MAIN/research/nord2/display-capabilities/hfsx-read.py --snapshot-receipt $X/snapshot/verified.json --update $X/offline --owned-json $X/owned.json --output $X/catalog.json > $X/catalog.log 2>&1; log catalog ok
ADDON=$(python3 -c "import json;print(json.load(open('$X/offline/verified.json'))['addon_sha256'])")
python3 $T/commit_root_delta_fast.py --main $MAIN --snapshot-receipt $X/snapshot/verified.json --update $X/offline --output $X/device-install --expected-addon-sha256 $ADDON --commit > $X/device-install.log 2>&1; log committed
$D/to_fastboot.sh 300; $D/flash_boot.sh $IMG; log kernel flashed $(sha256sum $IMG | cut -c1-16)
mkdir -p $D/runs/$2; python3 $W/tools/nord2-usb-console.py $D/runs/$2/ui-usb --wait 240 --read-seconds $READS > $D/runs/$2/usb.log 2>&1 &
CP=$!; for i in $(seq 1 $(( READS / 3 + 100 ))); do sleep 3; kill -0 $CP 2>/dev/null || break; done; wait $CP 2>/dev/null || true
log capture done
