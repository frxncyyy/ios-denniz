#!/bin/bash
# q10jfinal17bt = q10jfinal15 recipe (NOTES: usbchg fin3 build_q10jfinal3.sh, RAM8=0653, MDP final13 list + 0689 0690, scan_extra20,
# USB 0700..0703, 0790) + Bluetooth 0720 0721 0722 (new-file patches, used as they are) via the private combiner copy bt-20261115/fin3
# (builder bt-20261115/build_p1_candidate_bt.py = usbchg builder + the 0720-0722 block).  Do NOT wrap in flock (build_all.sh takes it).
B=$NORD2_WORK/gpu/build; D=$NORD2_WORK/gpu/build/bt-20261115; U=$NORD2_WORK/gpu/build/usbchg-20261115
M="$(sed -n 's/^MDP_DEFAULT="\(.*\)"$/\1/p' $D/fin3/build_q10jfinal3.sh) 0689:piece-0689 0690:piece-0690"
RAM8=0653 XPATCHES_FIN3="0720 0721 0722 0790" XDIRS_FIN3="$D/piece-0720 $D/piece-0721 $D/piece-0722 $B/autojoin-20261115/piece-0790" \
RUNCAM_FIN3=$D/fin3/run_integ_cam_f10.sh MDP="$M" SCAN=scan_extra20.txt \
EXTRA_PIECES="0700:$U/piece-0700 0701:$U/piece-0701 0702:$U/piece-0702 0703:$U/piece-0703" \
exec bash $D/fin3/build_q10jfinal3.sh "${1:-q10jfinal17bt}"
