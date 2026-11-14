#!/bin/bash
# gnss-20261114/pl4/gen.sh: concrete generators + outputs for gnssfix4 (safe) and gnssfix4x (experimental)
set -euo pipefail; cd "$(dirname "$0")"
for v in gnssfix4 gnssfix4x; do sed -e "s/gnssfix@/$v/g" -e "s/GNSSFIX@/${v^^}/g" -e "s/\"@\".endswith/\"$v\".endswith/" make_guests.py > make_guests-$v.py; python3 make_guests-$v.py | cut -c1-200; done
