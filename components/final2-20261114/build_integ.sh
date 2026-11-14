#!/bin/bash
# orchestrator integration build: usage build_integ.sh NAME "EXTRA_PATCHES" "EXTRA_DIRS" "EXTRA_SCANPATH_FUNCS"
B=$NORD2_WORK/gpu/build
NAME=$1
export SCANPATH_EXTRA_ALLOWED="_n2t_io_result _n2t_pmu_write _n2t_sysctl_cfg _n2t_sysctl_stat _n2td_close _n2td_open _n2td_write _ni_thread _nord2_input_init _nord2_input_prepare_return _n2scp_thread _n2rtc_fmt _n2rtc_pw_read _n2rtc_pw_write _n2rtc_read_time _n2rtc_sysctl_mode _n2rtc_sysctl_stat _n2rtc_udelay _n2rtc_write_once _nord2_rtc_boot_time _nat_thread _nat_usb $4"
export EXTRA_ARGS=n2gdv=9
export MORE_PATCHES="0491 0492 0494 0495 0496 0500 0501 0502 0530 0531 0540 0550 0551 0640 $2"
export MORE_DIRS="$B/wifi-fix-20261114 $B/ccperf-20261114 $B/gpuas-20261114/r0494 $B/bright-fix-20261114 $B/gpuas-20261114 $B/bltorch-20261114 $B/heat-20261114 $B/rtc-20261114 $B/cutout-20261114 $B/sbshift-20261114 $B/snap-20261114 $3"
exec bash $B/integ-20261114/run_integ.sh full $NAME $B/device-mmu-$NAME-candidate
