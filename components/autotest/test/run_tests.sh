#!/bin/bash
# Host tests of patch 0296: the command engine (t_nat.c, against a made-up kernel) and the picture packer (t_snap.cpp).
set -e
cd "$(dirname "$0")"
cc -O1 -g -Wall -Wextra -fsanitize=address,undefined -pthread -o t_nat t_nat.c && ./t_nat
sed -n '/^AppleCLCD::snap/,/^}/p' ../kernel/b/iokit/Kernel/arm/SweetFramebuffer.cpp | sed '1s/^/uint8_t *\n/' > snap_body.inc
c++ -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -o t_snap t_snap.cpp && ./t_snap
sed -e 's/#define NCL_ZONE_SIZE 0x40000u/#define NCL_ZONE_SIZE 0x1000u/' -e '/#include <kdp\/kdp_callout.h>/d' -e '/__asm__ volatile("dsb sy"/d' ../../crashlog-20261111/b/bsd/dev/nord2_crashlog.inc > crashlog_small.inc
cc -O1 -g -Wall -Wextra -Wno-unused-function -Wno-unused-variable -fsanitize=address,undefined -o t_crashlog t_crashlog.c && ./t_crashlog
rm -f t_nat t_snap snap_body.inc t_crashlog crashlog_small.inc
