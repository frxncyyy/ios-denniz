#!/bin/bash
# q10jmdp2a = q10jperf4 recipe (q10jfinal1 chain + 0660 + 0664 + 0665) + 0670 probe + 0671a (stages limited to 1: probe + DMA self-test)
# q10jmdp2  = q10jperf4 recipe + 0670 + 0671 (stages 1-3) + 0672 (AppleM2ScalerCSCDriver, registered only when stage 3 passed)
B=$NORD2_WORK/gpu/build; M=$NORD2_WORK/gpu/build/mdp-20261114
G="\"0660 $B/gpusched-20261114/piece-0660\" \"0664 $B/gpusched-20261114/piece-0664\" \"0665 $B/gpusched-20261114/piece-0665\" \"0670 $M/piece-0670\""
case "$1" in
  q10jmdp2a) G="$G \"0671 $M/piece-0671a\"";;
  q10jmdp2)  G="$G \"0671 $M/piece-0671\" \"0672 $M/piece-0672\"";;
  *) echo "usage: $0 q10jmdp2a|q10jmdp2"; exit 2;;
esac
GP_MORE="$G" MDP_SCAN="$(cat $M/scan_extra.txt)" exec bash $M/build_q10jmdp.sh "$1"
