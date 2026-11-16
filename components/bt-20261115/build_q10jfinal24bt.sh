#!/bin/bash
# q10jfinal24bt = q10jfinal23bt recipe with the bt6 pieces (LONGLOCAL 0x0c13/0x0c52, S10w watchdog, /chosen mac-address-bluetooth0). Was: q10jfinal23bt = q10jfinal21 recipe with the bt5 pieces 0720-0722 (private combiner bt-20261115/fin23). Was: q10jfinal21 = q10jfinal20 recipe (mdp-20261114/build_q10jfinal20.sh: q10jfinal19 + MDP 0691) + GPU 0750 (MMU-fault record line, trailing reports
#   are not orphans, revival of a FAULTED client: kern.n2gfrev / boot-arg n2gfrev=0 = old behaviour).  Private combiner copy gpufault-20261115/fin21
#   (copy of mdp-20261114/fin20), builder bt-20261115/build_p1_candidate_bt.py (unchanged). Do NOT wrap in flock.
B=$NORD2_WORK/gpu/build; D=$B/bt-20261115; U=$B/usbchg-20261115; S=$B/sim-20261115; G=$B/gpufault-20261115
MDP13="0670:piece-0670 0671:piece-0671a 0672:piece-0672 0673:piece-0673 0674:piece-0674 0675:piece-0675 0676:piece-0676 0677:piece-0677 0678:piece-0678 0679:piece-0679 0680:piece-0680 0681:piece-0681 0682:piece-0682 0683:piece-0683 0684:piece-0684 0685:piece-0685 0686:piece-0686 0687:piece-0687 0688:piece-0688"
RAM8=0653 XPATCHES_FIN3="0720 0721 0722 0790" XDIRS_FIN3="$D/piece-0720 $D/piece-0721 $D/piece-0722 $B/autojoin-20261115/piece-0790" \
RUNCAM_FIN3=$D/fin23/run_integ_cam_f10.sh MDP="$MDP13 0689:piece-0689 0690:piece-0690 0691:piece-0691" SCAN=scan_extra21.txt \
EXTRA_PIECES="0700:$U/piece-0700 0701:$U/piece-0701 0702:$U/piece-0702 0703:$U/piece-0703 0710:$S/piece-0710 0711:$S/piece-0711 0712:$S/piece-0712 0713:$S/piece-0713 0714:$S/piece-0714 0715:$S/piece-0715 0750:$G/piece-0750" \
exec bash $D/fin23/build_q10jfinal3.sh "${1:-q10jfinal24bt}"
