#!/bin/bash
# usbchg-20261115 private copy (paths -> bt-20261115/fin3); q10jfinal14usb = final13 recipe + EXTRA_PIECES 0700..0702
# final3-20261114/build_all.sh: main image = q10jcx recipe (final2-20261114/build_integ.sh) + 0560 camera flash strobe + 0580/0581 GNSS
# nodes + 0610 AW8697 haptics, with the camflash camera package (camflash-20261114/run_integ_cam.sh, pkg/audio-q10jcr.pkg sha 924feca7...).
#   usage: build_all.sh NAME ["EXTRA_PIECES"] ["EXTRA_PATCHES"] ["EXTRA_DIRS"] ["EXTRA_SCANPATH_FUNCS"]
#     EXTRA_PIECES  : '"NNNN /dir/with/NNNN-x.patch+patched-sources-NNNN" ...' = pieces that touch the post-0551 files: rebased by
#                     rebase.sh onto the chain (output in final3-20261114/p, included automatically, numbers added to the list)
#     EXTRA_PATCHES / EXTRA_DIRS: pieces used as they are (fuzz-0 on the chain, no rebase); any order, sorted here.
#   Steps: rebase.sh (writes final3-20261114/{chain,p}) -> run_integ_cam.sh full NAME $B/device-mmu-NAME-candidate under the global
#   heavy-job lock after free -m > 2500.  The output dir must not exist.  Expected end: the known 0399 IMAGECHECK stop, BUILD_EXIT 1.
set -euo pipefail
B=$NORD2_WORK/gpu/build
H=$B/bt-20261115/fin3/integ
NAME=${1:?NAME}; XPIECES=${2:-}; XPATCHES=${3:-}; XDIRS=${4:-}; XSCAN=${5:-}
O=$B/device-mmu-$NAME-candidate; [ ! -e $O ] || { echo "$O exists"; exit 1; }
PIECES="$XPIECES" bash $H/rebase.sh
PIECE_NUMS=$(ls $H/p)
SCAN_BASE="_n2t_io_result _n2t_pmu_write _n2t_sysctl_cfg _n2t_sysctl_stat _n2td_close _n2td_open _n2td_write _ni_thread _nord2_input_init _nord2_input_prepare_return _n2scp_thread _n2rtc_fmt _n2rtc_pw_read _n2rtc_pw_write _n2rtc_read_time _n2rtc_sysctl_mode _n2rtc_sysctl_stat _n2rtc_udelay _n2rtc_write_once _nord2_rtc_boot_time _nat_thread _nat_usb"
SCAN_CAMFLASH="_n2td_read _n2t_hw_strobe _n2t_publish"   # 0560: /dev/redwood-torch read() status (new in 0560)
SCAN_HAPTIC="_n2h_chip_init _n2h_hw_stop _n2h_latch _n2h_run _n2h_sysctl_cfg _n2h_sysctl_stat _n2h_sysctl_test _n2h_write _n2hd_close _n2hd_open _n2hd_write"
export SCANPATH_EXTRA_ALLOWED="$SCAN_BASE $(cat $B/gnss-20261114/scanpath_extra_gnss.txt) $SCAN_CAMFLASH $SCAN_HAPTIC $(cat $H/scanpath_extra_more.txt 2>/dev/null || true) $XSCAN"
export EXTRA_ARGS=${EXTRA_ARGS_OVERRIDE:-n2gdv=9}
export MORE_PATCHES="$(echo 0491 0492 0494 0495 0496 0500 0501 0502 0530 0531 0540 0550 0551 $PIECE_NUMS $XPATCHES | tr ' ' '\n' | grep . | sort -n -u | tr '\n' ' ' | sed 's/ $//')"
export MORE_DIRS="$B/wifi-fix-20261114 $B/ccperf-20261114 $B/gpuas-20261114/r0494 $B/bright-fix-20261114 $B/gpuas-20261114 $B/bltorch-20261114 $B/heat-20261114 $B/rtc-20261114 $B/cutout-20261114 $B/sbshift-20261114 $H/p $XDIRS"
echo "NAME $NAME"; echo "MORE_PATCHES $MORE_PATCHES"; echo "MORE_DIRS $MORE_DIRS"; echo "SCANPATH_EXTRA_ALLOWED $SCANPATH_EXTRA_ALLOWED"
set +e
flock $HOME/nord2-heavy.lock bash -c 'for i in $(seq 30); do a=$(free -m | awk "/^Mem:/{print \$7}"); [ $a -gt 2500 ] && break; echo "waiting for RAM ($a MB)"; sleep 20; done
  exec bash '${RUNCAM:-$B/camflash-20261114/run_integ_cam.sh}' full '$NAME' '$O
echo "BUILD_ALL_RC $?"
