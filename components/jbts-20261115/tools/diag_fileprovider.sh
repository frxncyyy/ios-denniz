#!/bin/bash
# Host only, read-only on the mirror: after ANY transaction's snapshot (the mirror then holds the phone's unified log), print the
# file-provider story of the newest boots: fileproviderd / Files / DocumentManager / LocalStorage / pkd messages (Default, Error,
# Fault), deduplicated per format string, in time order. Extract + decode take ~5 min and ~1.5 GB transient (deleted at the end).
# usage: jbts-20261115/tools/diag_fileprovider.sh [OUT.txt]
set -euo pipefail
B=$NORD2_WORK/gpu/build
U=$(mktemp -d -p $B/jbts-20261115 .fpdiag.XXXX); trap 'rm -rf "$U"' EXIT
OUT=${1:-$B/jbts-20261115/fpdiag-$(date +%m%d-%H%M).txt}
python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img rget /private/var/db/diagnostics $U/arc > /dev/null
python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img rget /private/var/db/uuidtext $U/arc > /dev/null
nice ~/.local/share/ulog-src/examples/target/release/unifiedlog_iterator -m log-archive -i $U/arc -o /dev/stdout 2>/dev/null |
  LC_ALL=C grep -a -E '"process":"[^"]*(fileproviderd|Files|DocumentManager|LocalStorage|pkd|TrollStore|runningboardd)' > $U/fp.jsonl || true
python3 -I $(dirname "$0")/fp_extract.py $U/fp.jsonl 2 > "$OUT"
echo "FPDIAG -> $OUT ($(wc -l < "$OUT") lines)"
