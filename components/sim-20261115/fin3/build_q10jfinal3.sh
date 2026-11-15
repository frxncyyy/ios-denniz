#!/bin/bash
# sim-20261115 private copy of usbchg-20261115/fin3 (q10jfinal16sim = final15 recipe + EXTRA_PIECES 0710..0712); was: usbchg-20261115 private copy (paths -> sim-20261115/fin3); q10jfinal14usb = final13 recipe + EXTRA_PIECES 0700..0702
# q10jfinal3/4... = q10jperf7f(a) (q10jperf6f chain + 0650 [+0651] RAM8 graphics heap) + MDP pieces, recipe = mdp-20261114/build_q10jmdpN.sh
# (private copy of integ3 in gpusched-20261114/fin3/integ + build_base.sh), n2gdv=73738 (clock acts, as q10jperf6f).
# Usage: [RAM8=0650|0653] [EXTRA_PIECES="NNNN:/dir ..."] [RUNCAM_FIN3=runner RAM8_OSFMK_OBJS=dir RAM8_STAGE=N (0652 osfmk objects)]
#        [RAM8=0650] [MDP="0670:piece-0670 ... 0678:piece-0678 0679:piece-0679 0680:piece-0680 0681:piece-0681"] [SCAN=scan_extraN.txt] build_q10jfinal3.sh NAME
#   RAM8=0650 -> heap 0650 only (the phone-proven ram8a heap); RAM8=0653 -> 0650+0651+0653 (q10jfinal9); default 0650+0651.
#   MDP: NNNN:dir pairs under mdp-20261114 (default = the q10jmdp18 list).  To add a later MDP piece, append it to MDP_DEFAULT (or pass MDP=)
#   and point SCAN_DEFAULT at the MDP agent's newest scan_extraN.txt (see its build_q10jmdpN.sh).
B=$NORD2_WORK/gpu/build; M=$B/mdp-20261114; R=$B/ram8-20261114; D=$B/gpusched-20261114
MDP_DEFAULT="0670:piece-0670 0671:piece-0671a 0672:piece-0672 0673:piece-0673 0674:piece-0674 0675:piece-0675 0676:piece-0676 0677:piece-0677 0678:piece-0678 0679:piece-0679 0680:piece-0680 0681:piece-0681 0682:piece-0682 0683:piece-0683 0684:piece-0684 0685:piece-0685 0686:piece-0686 0687:piece-0687 0688:piece-0688"
SCAN_DEFAULT=scan_extra18.txt
H="\"0650 $R/piece-0650\""; [ "${RAM8:-}" = 0650 ] || H="$H \"0651 $R/piece-0651\""
[ "${RAM8:-}" = 0653 ] && H="$H \"0653 $D/piece-0653\""          # RAM8=0653: 0650 + 0651 + 0653 (DT-checked CHECK regions, mb11b guard)
G="$H \"0660 $D/piece-0660\" \"0668 $D/piece-0668\""
for p in ${EXTRA_PIECES:-}; do G="$G \"${p%%:*} ${p#*:}\""; done      # EXTRA_PIECES="NNNN:/abs/dir ..." (e.g. 0652 driver piece, 0655 heap mask)
for p in ${MDP:-$MDP_DEFAULT}; do n=${p%%:*}; d=${p#*:}; [ -d "$M/$d" ] || { echo "missing MDP piece $M/$d"; exit 1; }; G="$G \"$n $M/$d\""; done
S=$M/${SCAN:-$SCAN_DEFAULT}; [ -f "$S" ] || { echo "missing $S"; exit 1; }
echo "FINAL3_PIECES $G"; echo "FINAL3_SCAN $S"
GP_MORE="$G" MDP_SCAN="$(cat $S) ${EXTRA_SCAN:-}" EXTRA_ARGS_OVERRIDE=${EXTRA_ARGS_OVERRIDE:-n2gdv=73738} exec bash $NORD2_WORK/gpu/build/sim-20261115/fin3/build_base.sh "${1:?NAME}"
