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
mk 0580 nord2-gnss-port a b bsd/dev/nord2_gnss_pwr.inc bsd/dev/nord2_gpsdl.inc bsd/dev/nord2_gnss_bind.inc bsd/dev/sweet_touch.c
mk 0581 nord2-gnss-pipe-guard b c bsd/dev/nord2_gpsdl.inc
#   0582 (c -> d): GPIO162 -> mode 2 GPS_L1_ELNA_EN at the K=31 HOLD (board DT gps_l1_lna_dsp_ctrl), kern.n2gnss bit 2 = skip.
mk 0582 nord2-gnss-l1-lna c d bsd/dev/nord2_gnss_bind.inc bsd/dev/nord2_gpsdl.inc
#   0583 (c -> e): persistent GNSS register trace in the ramoops console zone 0x4811F000 (survives a hard hang; read in the rescue
#        from /sys/fs/pstore/console-ramoops-0 with sweetlog_tail.sh); kern.n2gnss bit 3 = off. Independent of 0582.
mk 0583 nord2-gnss-persistent-trace c e bsd/dev/nord2_gpsdl.inc bsd/dev/nord2_gnss_bind.inc
#   0584 (e -> f): GPS EMI remap base 0xDFC00000 with the vendor reserved-memory layout (rings at +0x51000; the DSP's 1 MiB is free RAM,
#        DPM 0xDFE70000 no longer inside it) + DMA STATE read after each stop (trace).
mk 0584 nord2-gnss-emi-vendor-layout e f bsd/dev/nord2_gpsdl.inc bsd/dev/nord2_gnss_bind.inc
#   0585 (f -> g): = 0582 (GPIO162 -> GPS_L1_ELNA_EN at the K=31 HOLD) on top of 0583/0584, traced.
mk 0585 nord2-gnss-l1-lna-on-vendor-layout f g bsd/dev/nord2_gnss_bind.inc bsd/dev/nord2_gpsdl.inc
#   0586 (g -> h): ladder K=32 (ioctl 172) = steps 1..17 + data-link prep + HOLD WITHOUT the kernel DSP download: libmnl boots the
#        DSP itself over the pipe (vendor flow), so the DSP never receives a second post-boot 0xFE05.
mk 0586 nord2-gnss-libmnl-boots-dsp g h bsd/dev/nord2_gpsdl.inc
