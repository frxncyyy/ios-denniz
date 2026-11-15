#!/bin/bash
set -e; cd "$(dirname "$0")"
SRC=${SRC:-../r2/b/bsd/dev/nord2_scp.inc}
python3 -I carve.py $SRC n2scp_orient_classify n2scp_orient_set n2scp_orient_feed n2scp_motion_out n2scp_motion_target > carved.inc
cc -std=c11 -Wall -Wextra -Werror -o host_test host_test.c && ./host_test
