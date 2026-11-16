#!/bin/bash
# gfx100a: copy the per-process Nord2Metal file logs out of the MIRROR (read-only; the mirror is refreshed from the phone by every
# transaction's snapshot step, so run this after the next transaction -- any payload -- that follows a usage session), then summarise.
#   fetch_gfxlogs.sh OUTDIR
set -euo pipefail
B=$NORD2_WORK/gpu/build
O=${1:?usage: fetch_gfxlogs.sh OUTDIR}; mkdir -p "$O"
H="python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img"
NAMES=$($H ls /private/var/db/nord2-metal/log 2>/dev/null | awk '$NF ~ /\.log$/ {print $NF}')
[ -n "$NAMES" ] || { echo "no logs in /private/var/db/nord2-metal/log of the mirror"; exit 1; }
$H get /private/var/db/nord2-metal/log "$O" $NAMES > /dev/null
ls -la "$O"
echo "== gfx100a lines";              grep -h 'gfx100a\|GFX100A' "$O"/*.log | cut -c1-260 | sort | uniq -c | sort -rn | head -60
echo "== errors/warnings by process"; for f in "$O"/*.log; do echo "-- $(basename $f): $(grep -c ' E \| W ' $f) E/W lines"; grep -E '^[0-9:.]+ [EW] ' $f | cut -d' ' -f2- | sed -E 's/[0-9]+/N/g' | cut -c1-200 | sort | uniq -c | sort -rn | head -12; done
