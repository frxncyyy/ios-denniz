#!/bin/bash
set -euo pipefail; cd "$(dirname "$0")"
for v in gnssfix15x gnssfix15l gnssfix15v; do
  sed -e "s/gnssfix@/$v/g" -e "s/GNSSFIX@/${v^^}/g" -e "s/\"@\"/\"$v\"/g" make_guests.py > make_guests-$v.py; python3 make_guests-$v.py | cut -c1-100
done
