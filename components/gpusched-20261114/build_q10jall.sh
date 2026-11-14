#!/bin/bash
# q10jall* = q10jgp* (q10jperf1 recipe + 0630 + 0631) with the camprev-20261114 camera package (pkg/audio-camprev1.pkg, sha e86a64f2...) in place of
# camflash's audio-q10jcr.pkg, through gpusched-20261114/run_integ_cam_all.sh (= camprev-20261114/run_integ_cam.sh, builder file in this dir).
#   usage: [EXTRA_ARGS_OVERRIDE=n2gdv=...] bash build_q10jall.sh NAME
GP_MORE="${GP_MORE:-}" RUNCAM=$NORD2_WORK/gpu/build/gpusched-20261114/run_integ_cam_all.sh exec bash $NORD2_WORK/gpu/build/gpusched-20261114/build_q10jgp.sh "${1:?NAME}"
