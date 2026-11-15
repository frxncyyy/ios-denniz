#!/bin/bash
# q10jfinal25 = q10jfinal23bt recipe (bt-20261115/build_q10jfinal23bt.sh = q10jfinal21 + bt5 0720-0722) + 0760 SCP sensors to iOS
#   (accelerometer / gyroscope / compass / ambient light HID services, on-demand motion sampling, light rate fix, kern.n2scp_raw).
#   Private combiner copy sensors-20261115/fin25 (copy of bt-20261115/fin23, paths rewritten), builder bt-20261115/build_p1_candidate_bt.py
#   (unchanged). 0760 is a plain fuzz-0 patch (XPATCHES_FIN3/XDIRS_FIN3, like 0790): it touches only bsd/dev/nord2_scp.inc and
#   iokit/Kernel/arm/hid/SweetHID.cpp, which no piece above 0640 touches.  Do NOT wrap in flock (build_all.sh takes the heavy-job lock).
# Built 2026-11-15 21:10 with the bt6 versions of 0720-0722 (updated in place in bt-20261115 at 20:55-20:58), so the result is
#   q10jfinal24bt + 0760: its stage differs from device-mmu-q10jfinal24bt-candidate only in nord2_scp.inc and SweetHID.cpp; cmdline/ADT
#   identical; img 15304704 B sha256 66eb02b116ee59d2a50c9bf8c453c3c86ce2c80f31faeb505df578e40f06db56.
# Merge onto a later BT build: take ITS build script, add "0760" to XPATCHES_FIN3, "$B/sensors-20261115/piece-0760" to XDIRS_FIN3 and
#   EXTRA_SCAN="_n2scp_sysctl_raw"; if it touches nord2_scp.inc or SweetHID.cpp, regenerate 0760 (a/ = its stage files, re-apply b/, make_piece.sh).
B=$NORD2_WORK/gpu/build; D=$B/bt-20261115; U=$B/usbchg-20261115; S=$B/sim-20261115; G=$B/gpufault-20261115; N=$B/sensors-20261115
MDP13="0670:piece-0670 0671:piece-0671a 0672:piece-0672 0673:piece-0673 0674:piece-0674 0675:piece-0675 0676:piece-0676 0677:piece-0677 0678:piece-0678 0679:piece-0679 0680:piece-0680 0681:piece-0681 0682:piece-0682 0683:piece-0683 0684:piece-0684 0685:piece-0685 0686:piece-0686 0687:piece-0687 0688:piece-0688"
RAM8=0653 XPATCHES_FIN3="0720 0721 0722 0760 0790" XDIRS_FIN3="$D/piece-0720 $D/piece-0721 $D/piece-0722 $N/piece-0760 $B/autojoin-20261115/piece-0790" \
RUNCAM_FIN3=$N/fin25/run_integ_cam_f10.sh MDP="$MDP13 0689:piece-0689 0690:piece-0690 0691:piece-0691" SCAN=scan_extra21.txt EXTRA_SCAN="_n2scp_sysctl_raw ${EXTRA_SCAN:-}" \
EXTRA_PIECES="0700:$U/piece-0700 0701:$U/piece-0701 0702:$U/piece-0702 0703:$U/piece-0703 0710:$S/piece-0710 0711:$S/piece-0711 0712:$S/piece-0712 0713:$S/piece-0713 0714:$S/piece-0714 0715:$S/piece-0715 0750:$G/piece-0750" \
exec bash $N/fin25/build_q10jfinal3.sh "${1:-q10jfinal25}"
