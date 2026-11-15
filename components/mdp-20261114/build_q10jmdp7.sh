#!/bin/bash
# q10jmdp7 = q10jperf6 recipe (q10jfinal2f chain: X list of build_q10jmdp.sh + 0660 GPU step A + 0668 step C without the L2 policy 0664/0667)
#            + 0670 MDP probe + 0671a (stages: probe + DMA self-test S1, no service) + 0673 bring-up stage P + 0674 larb2 SMI bring-up, mutex-eof done, pipe dumps + 0675 larb2 check fix, short log lines + 0676 vendor RDMA/WROT words + WROT line ladder + 0677 WROT rotation offsets (default kern.n2mdp_stages 0x101)
B=$NORD2_WORK/gpu/build; M=$NORD2_WORK/gpu/build/mdp-20261114
G="\"0660 $B/gpusched-20261114/piece-0660\" \"0668 $B/gpusched-20261114/piece-0668\" \"0670 $M/piece-0670\" \"0671 $M/piece-0671a\" \"0673 $M/piece-0673\" \"0674 $M/piece-0674\" \"0675 $M/piece-0675\" \"0676 $M/piece-0676\" \"0677 $M/piece-0677\""
GP_MORE="$G" MDP_SCAN="$(cat $M/scan_extra7.txt)" exec bash $M/build_q10jmdp3_base.sh "${1:-q10jmdp7}"
