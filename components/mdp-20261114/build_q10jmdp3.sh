#!/bin/bash
# q10jmdp3 = q10jperf6 recipe (q10jfinal2f chain: X list of build_q10jmdp.sh + 0660 GPU step A + 0668 step C without the L2 policy 0664/0667)
#            + 0670 MDP probe + 0671a (stages: probe + DMA self-test S1, no service) + 0673 MDP bring-up stage P (default kern.n2mdp_stages 0x101)
B=$NORD2_WORK/gpu/build; M=$NORD2_WORK/gpu/build/mdp-20261114
G="\"0660 $B/gpusched-20261114/piece-0660\" \"0668 $B/gpusched-20261114/piece-0668\" \"0670 $M/piece-0670\" \"0671 $M/piece-0671a\" \"0673 $M/piece-0673\""
GP_MORE="$G" MDP_SCAN="$(cat $M/scan_extra3.txt)" exec bash $M/build_q10jmdp3_base.sh "${1:-q10jmdp3}"
