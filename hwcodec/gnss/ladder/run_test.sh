#!/bin/bash
# host simulation of the 0356 ladder (register model: vote/ack, BGF acks, GALS RDY, gps_top debug, D2AF3)
set -e; G=$(cd "$(dirname "$0")/.." && pwd); T=$(mktemp -d -p "$G/ladder" .t.XXXXXX); trap "rm -rf $T" EXIT
INC=${1:-$NORD2_WORK/gpu/build/gnss-dl-20261112/b/bsd/dev/nord2_gpsdl.inc}
awk "/^#ifndef N2G_USRT_BASE/{f=1} f" $INC | sed "/^n2gd_step(int step)/,\$d" | sed "\$d" > $T/ladder_part.c
cp $G/ladder/test_ladder.c $T/sim.c; grep -q 0x18c00014u $G/gnss_pwr.c; cc -std=gnu11 -Wall -Wno-unused-function -Wno-unused-label -Wno-format -I$G -I$T -DN2G_API="static __attribute__((unused))" $T/sim.c -o $T/sim; $T/sim
