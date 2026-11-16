#!/bin/bash
# q10jfinal26bt = q10jfinal25 recipe (sensors-20261115/build_q10jfinal25.sh = q10jfinal24bt + 0760 SCP sensors) with the bt7 pieces
#   0720-0722 (LE Set Scan Parameters: Scan Enable(0) first, 0x0c -> retry once -> status 0 = S10m; non-zero statuses S10y; command list
#   since HCI_Reset S10z at each bluetoothd disconnect; quirk 0x80, default 0xff). Private combiner copy bt-20261115/fin26 (= sensors fin25,
#   paths rewritten). Do NOT wrap in flock (build_all.sh takes the heavy-job lock).
B=$NORD2_WORK/gpu/build; D=$B/bt-20261115; U=$B/usbchg-20261115; S=$B/sim-20261115; G=$B/gpufault-20261115; N=$B/sensors-20261115
MDP13="0670:piece-0670 0671:piece-0671a 0672:piece-0672 0673:piece-0673 0674:piece-0674 0675:piece-0675 0676:piece-0676 0677:piece-0677 0678:piece-0678 0679:piece-0679 0680:piece-0680 0681:piece-0681 0682:piece-0682 0683:piece-0683 0684:piece-0684 0685:piece-0685 0686:piece-0686 0687:piece-0687 0688:piece-0688"
RAM8=0653 XPATCHES_FIN3="0720 0721 0722 0760 0790" XDIRS_FIN3="$D/piece-0720 $D/piece-0721 $D/piece-0722 $N/piece-0760 $B/autojoin-20261115/piece-0790" \
RUNCAM_FIN3=$D/fin26/run_integ_cam_f10.sh MDP="$MDP13 0689:piece-0689 0690:piece-0690 0691:piece-0691" SCAN=scan_extra21.txt EXTRA_SCAN="_n2scp_sysctl_raw ${EXTRA_SCAN:-}" \
EXTRA_PIECES="0700:$U/piece-0700 0701:$U/piece-0701 0702:$U/piece-0702 0703:$U/piece-0703 0710:$S/piece-0710 0711:$S/piece-0711 0712:$S/piece-0712 0713:$S/piece-0713 0714:$S/piece-0714 0715:$S/piece-0715 0750:$G/piece-0750" \
exec bash $D/fin26/build_q10jfinal3.sh "${1:-q10jfinal26bt}"
