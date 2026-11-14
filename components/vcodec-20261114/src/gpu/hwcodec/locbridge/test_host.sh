#!/bin/bash
# SPDX-License-Identifier: MIT
# host tests of the location bridge: nmea.c unit test, n2locbridge -DN2_HOST replay of sample-drive.nmea, and the FIFO path
# (a writer feeds the replay file into the FIFO). Prints LOCBRIDGE HOST TEST OK.
set -euo pipefail
D=$(cd "$(dirname "$0")" && pwd); T=$(mktemp -d)
cc -O1 -Wall -Wextra -o $T/test_nmea $D/test_nmea.c $D/nmea.c -lm; $T/test_nmea
cc -O1 -Wall -Wextra -Wno-builtin-declaration-mismatch -DN2_HOST -o $T/bridge $D/n2locbridge.c $D/nmea.c
$T/bridge --replay $D/sample-drive.nmea --period-ms 0 > $T/replay.out
cat $T/replay.out | head -3; tail -1 $T/replay.out
n=$(grep -c '^N2LOC FIX' $T/replay.out); [ "$n" = 20 ] || { echo "replay: $n fixes, want 20"; exit 1; }
grep -q 'replay done fixes=20 lines=61 bad=1' $T/replay.out
grep -m1 '^N2LOC FIX' $T/replay.out | grep -q 'lat=41.902800 lon=12.496400 alt=30.0 hacc=4.0 vacc=6.4 speed=1.39 course=45.0 utc_ms=1794478500000 sats=9 q=1'
# FIFO: the bridge listens, a writer sends the log, fixes are rate-limited to one per --min-interval-ms
$T/bridge --fifo $T/gnss.fifo --min-interval-ms 0 > $T/fifo.out & P=$!
for i in $(seq 50); do [ -p $T/gnss.fifo ] && break; sleep 0.1; done
cat $D/sample-drive.nmea > $T/gnss.fifo; sleep 0.5; cat $D/sample-drive.nmea > $T/gnss.fifo; sleep 0.5
kill $P; wait $P 2>/dev/null || true
m=$(grep -c '^N2LOC FIX' $T/fifo.out); [ "$m" = 40 ] || { echo "fifo: $m fixes, want 40"; cat $T/fifo.out | tail; exit 1; }
grep -q 'listening on' $T/fifo.out
rm -rf $T; echo "LOCBRIDGE HOST TEST OK"
