#!/bin/bash
# gnss-20261114: 0580 nord2-gnss-port + 0581 nord2-gnss-pipe-guard (2026-11-14).
# Base a/ = the q10jcx stage (device-mmu-q10jcx-candidate/k/kernel/stage; its sweet_touch.c is byte-identical to q10jcp's).
#   0580 (a -> b): bsd/dev/nord2_gpsdl.inc + nord2_gnss_pwr.inc = the 12 Nov GNSS line (0350..0364 final files, gnss-dl4/gnss-dl
#        b/), bsd/dev/nord2_gnss_bind.inc = its sweet_touch.c binding block (0351..0363) moved to a file (AO pages: Wi-Fi's nw.*
#        io_submap mappings reused, io_submap controls removed), sweet_touch.c: 1 include + 1 start call; nodes on by default; kill switch sysctl kern.n2gnss bit 0 (n2gdv bits all taken)
#        (no boot-arg).
#   0581 (b -> c): nord2_gpsdl.inc HOLD pipe hardening (gps_top_off gate, transient refusals, quiet TX, heartbeat; kern.n2gnss bit 1
#        = observe only).
# Host tests: test/run.sh.  Nothing here writes outside this directory.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
T=$(mktemp -d -p "$H" .mk.XXXXXX); trap 'rm -rf "$T"' EXIT
mk() { n=$1; name=$2; from=$3; to=$4; shift 4; out=$H/$n-$name.patch; : > "$out"
  for f in "$@"; do
    if [ -f "$from/$f" ]; then diff -u "$from/$f" "$to/$f" | sed -E "1s#^--- .*#--- a/$f#; 2s#^\+\+\+ .*#+++ b/$f#" >> "$out" || true
    else diff -u /dev/null "$to/$f" | sed -E "1s#^--- .*#--- /dev/null#; 2s#^\+\+\+ .*#+++ b/$f#" >> "$out" || true; fi
  done
  rm -rf "$T/s"; mkdir "$T/s"; cp -a $from/. "$T/s/"
  (cd "$T/s" && patch -p1 --fuzz=0 --batch -i "$out") > "$H/apply-$n.log"
  ! grep -E 'offset|fuzz|FAILED|Reversed' "$H/apply-$n.log"
  rm -rf "$H/patched-sources-$n"
  for f in "$@"; do cmp "$T/s/$f" "$to/$f"; mkdir -p "$H/patched-sources-$n/$(dirname "$f")"; cp "$to/$f" "$H/patched-sources-$n/$f"; done
  # io_submap (3 free pages): no new plain ml_io_map( call; ml_io_map_unmappable only
  add=$(grep '^+' "$out" | grep -v '^+++' | sed 's/ml_io_map_unmappable//g' | grep -c 'ml_io_map(' || true)
  [ "$add" = 0 ] || { echo "$n adds $add plain ml_io_map( lines"; exit 1; }
  sha256sum "$out"
}
#   0587 (h -> i): K=33 S13 pacing probe (K=31 + 200 ms idle gap after fragment 1 with 1 ms state samples) + pipe gate samples
#        conn_infra slp_prot (0x18060184) and BUS_TIMEOUT_IRQ (0x180602d4, allow-listed) into the 0583 trace; kern.n2gnss bit 4 = off.
mk 0587 nord2-gnss-pacing-probe h i bsd/dev/nord2_gpsdl.inc bsd/dev/nord2_gnss_pwr.inc
#   0588 (i -> j): K=34..36 = K=32 HOLD variants that mimic the S11 loop: no LNA before the boot (bit 0), fresh 64 B RX arming before each pipe TX (bit 1), 2 s settle.
mk 0588 nord2-gnss-hold-like-s11 i j bsd/dev/nord2_gpsdl.inc
#   0589 (j -> k): K=37/38 = K=34 + quiet pipe (no gate register reads, no console lines) + RX round closed/re-armed on data (S11-exact) + GPS GIC SPIs logged/masked at HOLD; K=38 also traces the GIC words around every pipe TX.
mk 0589 nord2-gnss-quiet-pipe j k bsd/dev/nord2_gpsdl.inc bsd/dev/nord2_gnss_bind.inc
#   0590 (k -> l): K=39 = K=37 with the pipe TX done by the ladder n2gl_tx (readbacks + printf before the done poll); K=40 = no-poll pipe TX (200 us wait, then sparse done checks).
mk 0590 nord2-gnss-tx-no-poll k l bsd/dev/nord2_gpsdl.inc
#   0591 (l -> m): cpu id + kernel addresses in the 0583 trace (post-mortem last-PC symbolisation); K=41 HOLD + ioctl 190 = boot frame i through the ladder n2gb_xchg.
mk 0591 nord2-gnss-xchg-ioctl-cpu-trace l m bsd/dev/nord2_gpsdl.inc
#   0592 (m -> n): every ring access through a volatile byte view (the vectorised LDR Q ring copy in n2gd_read = unaligned Device access); K=42 = K=41 + close-on-data, K=43 = K=41 + ladder TX in the pipe.
mk 0592 nord2-gnss-ring-bytewise m n bsd/dev/nord2_gpsdl.inc
#   0593 (n -> o): ioctl 192 = GPIO162 -> mode 2 (L1 eLNA to the DSP) on demand, after the boot.
mk 0593 nord2-gnss-lna-ioctl n o bsd/dev/nord2_gpsdl.inc
