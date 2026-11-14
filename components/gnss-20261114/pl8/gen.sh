#!/bin/bash
# pl8 (0587): gnssfix8 (K=32 + read poll 1 ms) and gnssfix8k (K=33 S13 in-kernel pacing probe, writes dropped)
set -euo pipefail; cd "$(dirname "$0")"
for v in gnssfix8 gnssfix8k; do
  sed -e "s/gnssfix@/$v/g" -e "s/GNSSFIX@/${v^^}/g" -e "s/\"@\".endswith/\"$v\".endswith/g" make_guests.py > make_guests-$v.py; python3 make_guests-$v.py | cut -c1-140
done
