#!/bin/bash
# work/a + work/b -> ../0450-*.patch, ../0451-*.patch and ../patched-sources-0450/0451 (the files each patch leaves).
set -euo pipefail
cd "$(dirname "$0")"; ./regen.sh
P0="bsd/dev/wifi_8021x.h bsd/dev/wifi_ios_join.c bsd/dev/wifi_ios_join.h bsd/dev/wifi_ios_station_view.c bsd/dev/wifi_station_transaction.c bsd/dev/wifi_station_transaction.h bsd/dev/wifi_station_wire.c bsd/dev/wifi_station_wire.h bsd/dev/wifi_wpa2.c bsd/dev/wifi_wpa2.h"
P1="bsd/dev/wifi_8021x_bind.inc bsd/dev/wifi_ifnet_link.inc bsd/dev/wifi_station_service.h bsd/dev/wifi_station_service_bind.inc bsd/dev/wifi_station_transport_bind.inc iokit/Kernel/arm/wifi_ios_client.cpp"
mk() { n=$1; name=$2; shift 2; out=../$n-$name.patch; : > $out; rm -rf ../patched-sources-$n
  for f in "$@"; do mkdir -p ../patched-sources-$n/$(dirname $f); cp b/$f ../patched-sources-$n/$f
    if [ -f a/$f ]; then diff -u --label a/$f --label b/$f a/$f b/$f >> $out || true; else diff -u --label /dev/null --label b/$f /dev/null b/$f >> $out || true; fi; done
  echo "$out: $(grep -c '^+++ ' $out) files, +$(grep -c '^+[^+]' $out) -$(grep -c '^-[^-]' $out)"; }
mk 0450 nord2-wifi-8021x-engine $P0
mk 0451 nord2-wifi-8021x-eapol-supplicant $P1
# every changed file is in exactly one patch
( cd b && find . -type f | sed 's#^\./##' | while read -r f; do cmp -s $f ../a/$f 2>/dev/null || echo $f; done | sort ) > /tmp/.n8x.changed.$$
echo $P0 $P1 | tr ' ' '\n' | sort | diff - /tmp/.n8x.changed.$$ && echo "patch file lists = changed files"; rm -f /tmp/.n8x.changed.$$
