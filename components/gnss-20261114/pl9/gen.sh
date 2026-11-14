#!/bin/bash
set -euo pipefail; cd "$(dirname "$0")"
for v in gnssfix9a gnssfix9b gnssfix9c; do
  sed -e "s/gnssfix@/$v/g" -e "s/GNSSFIX@/${v^^}/g" -e "s/\"@\"/\"$v\"/g" make_guests.py > make_guests-$v.py; python3 make_guests-$v.py | cut -c1-120
done
