#!/bin/bash
set -euo pipefail; cd "$(dirname "$0")"
v=gnssfix7; sed -e "s/gnssfix@/$v/g" -e "s/GNSSFIX@/${v^^}/g" -e "s/\"@\".endswith/\"$v\".endswith/g" make_guests.py > make_guests-$v.py; python3 make_guests-$v.py | cut -c1-140
