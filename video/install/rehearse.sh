#!/bin/bash
# SPDX-License-Identifier: MIT
# LEAD. Runs a guest script on a throw-away qcow2 overlay of the root file system mirror (the same VM step txn.sh runs, then
# the catalog check), without any phone. usage: rehearse.sh NAME [GUEST=guest-video.sh] [PAYLOAD=install/payload] [BASE_UPDATE_DIR]
# Output in gpu/video/build/rehearsal/NAME (the overlay and initrd are deleted at the end unless KEEP=1).
set -euo pipefail
V=$(cd "$(dirname "$0")/.." && pwd); W=$(cd "$V/../.." && pwd); B=$W/gpu/build; T=$W/gpu/tools
MAIN=$NORD2_REPO
NAME=$1; GUEST=${2:-$V/install/guest-video.sh}; PAYLOAD=${3:-$V/install/payload}
X=$V/build/rehearsal/$NAME; mkdir -p "$V/build/rehearsal"; rm -rf "$X"
# A receipt for the mirror AS IT IS NOW (a real transaction's snapshot receipt goes stale at its own commit): made from the
# mirror's side manifest, marked as a rehearsal. It is never given to commit_root_delta_fast.py.
SNAP=${SNAP:-$V/build/rehearsal/$NAME.snapshot.json}
[ -f "$SNAP" ] || python3 - "$B/mirror/rootfs-mirror.img" "$SNAP" <<'PY'
import json, sys, os
m, out = sys.argv[1:3]; side = json.load(open(m + '.manifest.json')); st = os.stat(m)
assert side['clean'] and side['signature'] == {'size': st.st_size, 'mtime_ns': st.st_mtime_ns, 'ino': st.st_ino}, 'mirror is being changed'
json.dump({'format': 'nord2-fastroot-1', 'kind': 'snapshot', 'rehearsal_only': True, 'mirror': m, 'bytes': st.st_size, 'root': side['root'],
           'per_chunk_manifest_matches': True, 'phone_partition_writes': 0}, open(out, 'w'), indent=1)
PY
echo "snapshot receipt: $SNAP"
python3 $B/apps-20261104/generic_update_offline.py --snapshot-receipt "$SNAP" --payload "$PAYLOAD" --guest "$GUEST" --output "$X" \
  --initramfs $MAIN/research/nord2/rootfs-vm/initramfs > "$X.offline.log" 2>&1 || { tail -40 "$X.offline.log"; exit 1; }
grep -a -E "VIDEO_|CC_JOB|CC_RECEIPT|appears to be OK|Traceback|Error|assert" "$X/vm.log" || true
python3 $T/check_update_catalog_fast.py --hfsx-read $MAIN/research/nord2/display-capabilities/hfsx-read.py --snapshot-receipt "$SNAP" \
  --update "$X" --owned-json "$V/install/owned-video.json" --output "$X/catalog.json" > "$X/catalog.log" 2>&1 || { echo CATALOG FAILED; cat "$X/catalog.log" | tail -5; cat "$X/catalog.json"; exit 1; }
cat "$X/catalog.json"; echo
[ "${KEEP:-0}" = 1 ] || rm -f "$X/overlay.qcow2" "$X/initrd.gz" "$X/delta.bin"
echo REHEARSAL_OK "$X"
