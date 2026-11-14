#!/bin/bash
# gnss-20261114/pl5/gen.sh: concrete generators + outputs for gnssfix5 (safe, new binary) and gnssfix5n (no device reads)
set -euo pipefail; cd "$(dirname "$0")"
for v in gnssfix5 gnssfix5n; do sed -e "s/gnssfix@/$v/g" -e "s/GNSSFIX@/${v^^}/g" -e "s/\"@\".endswith/\"$v\".endswith/" make_guests.py > make_guests-$v.py; python3 make_guests-$v.py | cut -c1-200; done
