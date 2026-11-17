#!/bin/bash
# host test for audio_call.c: cd here and run
set -e
cc -std=gnu11 -O1 -g -fsanitize=address,undefined '-DNAC_DMB_LD()=' '-DNAC_DMB_ST()=' '-DNAC_DSB()=' -Wall -Wextra -Wno-unused-function -Wno-unused-parameter \
  -Istub -I../src -I$NORD2_WORK/shim/nord2-xnu -I$NORD2_WORK/gpu/build/xnu-stage-backup-pre-p15-20261028/xnu-stage/bsd/dev -o /tmp/claude-1000/test_call_bin test_call.c
/tmp/claude-1000/test_call_bin | tail -3
