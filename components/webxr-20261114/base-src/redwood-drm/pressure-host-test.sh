#!/bin/bash
# y2s95: nord2_pan.c (this dir) against the kernel model of gpu/y2s/nord2_pan_test.c + the pressure scenario (drain, hold-off, refill after)
set -euo pipefail
D=$(cd "$(dirname "$0")" && pwd); T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
mkdir -p "$T/y2s" "$T/include"; cp "$D/nord2_pan.c" "$D/nord2_pan.h" "$D/nord2_gpu_transport.h" "$T/y2s/"; cp "$D/nord2_gpu_transport.h" "$T/include/"
cp "$D/nord2_pan_pressure_host_test.c" "$T/y2s/t.c"; gcc -O1 -g -Wall -pthread -o "$T/t" "$T/y2s/t.c" "$T/y2s/nord2_pan.c"; "$T/t"
