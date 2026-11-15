#!/bin/bash
# q10jfinal16sim = q10jfinal15 recipe (usbchg-20261115/build_q10jfinal15.log) + SIM pieces 0710..0712, private combiner copy sim-20261115/fin3.
# Do NOT wrap in flock (build_all.sh takes nord2-heavy.lock itself).
B=$NORD2_WORK/gpu/build; U=$NORD2_WORK/gpu/build/usbchg-20261115; S=$NORD2_WORK/gpu/build/sim-20261115
MDP13="0670:piece-0670 0671:piece-0671a 0672:piece-0672 0673:piece-0673 0674:piece-0674 0675:piece-0675 0676:piece-0676 0677:piece-0677 0678:piece-0678 0679:piece-0679 0680:piece-0680 0681:piece-0681 0682:piece-0682 0683:piece-0683 0684:piece-0684 0685:piece-0685 0686:piece-0686 0687:piece-0687 0688:piece-0688"
NAME=${1:-q10jfinal16sim}
# q10jfinal16sim2 = + 0713 0714: SIM_MORE=" 0713:$S/piece-0713 0714:$S/piece-0714" build_q10jfinal16sim.sh q10jfinal16sim2
RAM8=0653 XPATCHES_FIN3=0790 XDIRS_FIN3=$B/autojoin-20261115/piece-0790 RUNCAM_FIN3=$S/fin3/run_integ_cam_f10.sh \
MDP="$MDP13 0689:piece-0689 0690:piece-0690" SCAN=scan_extra20.txt \
EXTRA_PIECES="0700:$U/piece-0700 0701:$U/piece-0701 0702:$U/piece-0702 0703:$U/piece-0703 0710:$S/piece-0710 0711:$S/piece-0711 0712:$S/piece-0712${SIM_MORE:-}" \
exec bash $S/fin3/build_q10jfinal3.sh "$NAME"
