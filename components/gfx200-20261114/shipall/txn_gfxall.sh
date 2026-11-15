#!/bin/bash
# NOT EXECUTED BY ITS AUTHOR. ONE transaction of the INTEGRATED userland (gfx200-20261114/shipall; copy of gfx100a's txn_gfx100a.sh):
#   gfxall1 = shader99b + webxr1 + gfx100a + bigpool1 + gpupipe1 (Nord2Metal e6f75db1, library 366b0e1a)
#   gfxall2 = gfxall1 + gfx200 (msaartt, zsdc, rtpool, pools)       (Nord2Metal abf0e80b, library f25ca5e4)
#   gfxall17 = gfxall16 + FIX landscape-only apps (Terraria): rotated display-surface textures served (own landscape texture + GPU turn into
#     the surface); gfxall17-seeded = + read-only shader cache; gfxall17-flip-seeded = + gfxall17-rot-flip (other direction); gfxall17-norot =
#     no-gfxall17-rot + surf-all + log dir (A/B); gfxall17-diag = + surf-all + log dir; gfxall17-off = every gfxall feature off
#   gfxall16 = gfxall15 + FIX: the 'b3a8' colour plane checked as a full-size 4-byte plane (gfxall15 refused it with 4:2:0 rules)
#   gfxall15 = gfxall14 + FIX Safari transparent layers ('b3a8': QuartzCore's Metal-limits Apple3 site answered YES + two-plane 'b3a8' colour
#     plane served in place); gfxall15-nob3a8 = no-b3a8 + surf-all + log dir (A/B); gfxall15-diag = + surf-all + log dir
#   gfxall14 = gfxall13 + Metal-source compiles logged + 8 s limit in com.apple.WebKit.GPU (WebGL can no longer freeze the page) + WebKit crash
#     backtraces (log dir); gfxall14-diag = + surf-all + log dir; gfxall14-nolimit = no-msltimeout + surf-all + log dir (A/B)
#   gfxall13 = gfxall12 + FIX Safari: XR Metal formats over 'w30r'/'b3a8' surfaces served in place (8-bit alias); gfxall13-noxr = refused as before
#     (+ surf-all + log dir, for the A/B); gfxall13-diag = + surf-all + log dir; gfxall13-seeded = + read-only shader cache
#   gfxall12 = gfxall11 + diag switch surf-all; gfxall12-diag = + surf-all + log dir (Safari: every IOSurface texture request, deduplicated, centre bytes)
#   gfxall11 = FIX: the App Library's settle snapshot refused by default in SpringBoard (live blur kept; matsnap-allow = old way), diagnostics
#     off (switches still read); gfxall11-off = every gfxall feature off + matsnap-allow; gfxall11-seeded = + read-only shader cache (374)
#   gfxall10 = gfxall9 + SpringBoard hook of -[MTMaterialLayer setContentReplacedWithSnapshot:]; gfxall10-diag = surf-al + trace-al + matsnap-log + log dir;
#     gfxall10-nosnap = diag + no-matsnap (the App Library keeps its live backdrop instead of the settle snapshot); any gfxall10 removes gfxall9-bgra's MG key
#     gfxall10-nodeep = diag + mt-nodeep (CoreMaterial MTSupportsDeepColor() forced NO in SpringBoard: BGRA snapshots)
#   gfxall9 = gfxall8 + Nord2Metal diag surf-al (off unless its switch is there); gfxall9-diag = + surf-al + trace-al + log dir (IOSurface/texture
#     requests, renders into surfaces, centre bytes, MobileGestalt deep-colour answer); gfxall9-bgra = diag + MobileGestalt CacheExtra deep colour
#     = false (CoreMaterial snapshots 'BGRA' instead of 'w30r'; taken back by any other gfxall9 variant / the undo); *-seeded = + read-only shader cache
#   gfxall8 = gfxall7 (same library ec36ec3e) + Nord2Metal diag trace-al (off unless its switch file is there); gfxall8-diag = + trace-al + log dir
#     (backboardd traces the App Library backdrop from its first frame to 60 buffers after its last, texels each step reads, chain buffer bytes);
#     gfxall8-diag-nofb = diag + no-rt1 + no-singletarget (y2s96 framebuffer tricks off); gfxall8-diag-off = diag + every gfxall/y2s96 switch off
#   gfxall7-diag = gfxall7 + /var/db/nord2-metal/trace-cc (backboardd traces 3 command buffers draw by draw, with pixel probes, every 15 s from
#             60 s after its first commit, 20 times) + the per-process file log directory /var/db/nord2-metal/log (fetch_gfxlogs.sh after the next txn)
#   gfxall7 = gfxall6 + FIX: vertex/tiler chains declare every buffer of their batch (no overlap of a pass with the pass it samples) (library ec36ec3e)
#   gfxall6 = gfxall5 + FIX of gpupipe1's tiler-heap bookkeeping (wrong heap in precise chains -> backboardd MMU fault, frozen UI) (library 937911aa)
#   gfxall5 = gfxall4 + gpupipe2 (ABI v4, 128 buffers per job, v3/v2 fallback; kernel 0660 = q10jperf2/q10jfinal2) (library 71a88a19)
#   gfxall4 = gfxall3 + push of every constant buffer, one pool budget + pressure release, async pipeline builds everywhere,
#             read-only shader cache /usr/lib/nord2-metal-cache (gfxall4-seeded: built by seed_pancache.sh from the phone's cache)
#   gfxall3 = gfxall2 + shader file cache, state cache, upload batching, bytes ring (Nord2Metal 744e3fa9, library 0fb23f5e)
# then flash KERNEL_IMG. Accepts the phone in shader99b (the mirror) or any single-payload state of tonight (webxr1, gfx100a, bigpool1/1x,
# gpupipe1) or gfxall1 (for gfxall2). It writes the phone: run it only with the user's go.
# usage (phone already in fastboot):
#   [VARIANT=gfxall17|gfxall17-seeded|gfxall17-flip-seeded|gfxall17-diag|gfxall17-norot|gfxall17-off|gfxall16|gfxall16-diag|gfxall16-nob3a8|gfxall16-off|gfxall16-seeded|gfxall15|gfxall15-diag|gfxall15-nob3a8|gfxall15-off|gfxall15-seeded|gfxall14|gfxall14-diag|gfxall14-nolimit|gfxall14-off|gfxall14-seeded|gfxall13|gfxall13-diag|gfxall13-noxr|gfxall13-off|gfxall13-seeded|gfxall12|gfxall12-diag|gfxall12-off|gfxall11|gfxall11-off|gfxall11-seeded|gfxall10|gfxall10-diag|gfxall10-nosnap|gfxall10-nodeep|gfxall10-off|gfxall9|gfxall9-diag|gfxall9-bgra|gfxall9-off|gfxall9-seeded|gfxall8|gfxall8-diag|gfxall8-diag-nofb|gfxall8-diag-off|gfxall8-off|gfxall8-seeded|gfxall7|gfxall7-diag|gfxall7-off|gfxall7-nopipe|gfxall7-nov4|gfxall7-seeded|gfxall6|gfxall6-off|gfxall6-nopipe|gfxall6-nov4|gfxall6-seeded|gfxall5|gfxall5-off|gfxall5-nov4|gfxall5-nopush|gfxall5-seeded|gfxall4|gfxall4-off|gfxall4-nopush|gfxall4-seeded|gfxall3|gfxall3-off|gfxall3-nocache|gfxall3-nostate|gfxall2|gfxall2-off|gfxall2-nortt|gfxall2-nopools|gfxall1|gfxall1-nopipe] READS=600 txn_gfxall.sh KERNEL_IMG TAG
#   UNDO=1 [VARIANT=gfxall17|gfxall16|gfxall15|gfxall14|gfxall13|gfxall12|gfxall11|gfxall10|gfxall9|gfxall8|gfxall7|gfxall6|gfxall5|gfxall4|gfxall3|gfxall2|gfxall1] txn_gfxall.sh KERNEL_IMG TAG   back to the pair the record names (fallback copies: shader99b)
set -e
trap 'echo "$(date -Is) FAILED at line $LINENO" | tee -a $X/events' ERR
W=$NORD2_WORK
B=$W/gpu/build; T=$W/gpu/tools; S=$B/gfx200-20261114/shipall; MAIN=$NORD2_REPO
A=$HOME/Android/Sdk/platform-tools/adb; D=$B/autocycle; IMG=$1; X=$B/cc-txn-$2
[ -n "$2" ] && [ -f "$IMG" ] || { echo "usage: txn_gfxall.sh KERNEL_IMG TAG"; exit 2; }
[ ! -e "$X" ] || { echo "$X exists: pick a new TAG"; exit 2; }
READS=${READS:-600}; [ "$READS" -gt 0 ] && [ "$READS" -le 600 ] || { echo "READS must be 1..600 (nord2-usb-console.py limit)"; exit 2; }
V=${VARIANT:-gfxall2}; case $V in gfxall17|gfxall17-seeded|gfxall17-flip-seeded|gfxall17-diag|gfxall17-norot|gfxall17-off|gfxall16|gfxall16-diag|gfxall16-nob3a8|gfxall16-off|gfxall16-seeded|gfxall15|gfxall15-diag|gfxall15-nob3a8|gfxall15-off|gfxall15-seeded|gfxall14|gfxall14-diag|gfxall14-nolimit|gfxall14-off|gfxall14-seeded|gfxall13|gfxall13-diag|gfxall13-noxr|gfxall13-off|gfxall13-seeded|gfxall12|gfxall12-diag|gfxall12-off|gfxall11|gfxall11-off|gfxall11-seeded|gfxall10|gfxall10-diag|gfxall10-nosnap|gfxall10-nodeep|gfxall10-off|gfxall9|gfxall9-diag|gfxall9-bgra|gfxall9-off|gfxall9-seeded|gfxall8|gfxall8-diag|gfxall8-diag-nofb|gfxall8-diag-off|gfxall8-off|gfxall8-seeded|gfxall7|gfxall7-diag|gfxall7-off|gfxall7-nopipe|gfxall7-nov4|gfxall7-seeded|gfxall6|gfxall6-off|gfxall6-nopipe|gfxall6-nov4|gfxall6-seeded|gfxall5|gfxall5-off|gfxall5-nov4|gfxall5-nopush|gfxall5-seeded|gfxall4|gfxall4-off|gfxall4-nopush|gfxall4-seeded|gfxall3|gfxall3-off|gfxall3-nocache|gfxall3-nostate|gfxall2|gfxall2-off|gfxall2-nortt|gfxall2-nopools|gfxall1|gfxall1-nopipe) ;; *) echo "bad VARIANT $V"; exit 2;; esac
NM=${V%%-*}; P=$V; G=$S/guest-$NM.sh; OKRE="${NM^^}_INSTALLED\|${NM^^}_ALREADY"
if [ "${UNDO:-0}" = 1 ]; then P=$NM-undo; G=$S/guest-$NM-undo.sh; OKRE="${NM^^}_UNDONE"; fi
O=$S/owned-gfxall.json; case $P in *-seeded) O=$S/owned-$P.json;; esac
mkdir -p $X
log(){ echo "$(date -Is) $*" | tee -a $X/events; }
log start $P; $D/to_fastboot.sh ${FB_WAIT:-1800}
$D/flash_boot.sh $MAIN/research/nord2/rescue/nord2-adb-rescue.img
for i in $(seq 1 40); do sleep 3; timeout 3 $A devices | grep -q "recovery$" && break; done; log rescue up
python3 $T/snapshot_current_root_fast.py --main $MAIN --mirror $B/mirror/rootfs-mirror.img --output $X/snapshot --jobs 4 > $X/snapshot.log 2>&1; log snapshot done
python3 $B/apps-20261104/generic_update_offline.py --snapshot-receipt $X/snapshot/verified.json --payload $S/payload-$P --guest $G --output $X/offline --initramfs $MAIN/research/nord2/rootfs-vm/initramfs > $X/offline.log 2>&1; log offline done
grep -a -q "AssertionError\|Traceback" $X/offline/vm.log && { log "guest refused or failed: see offline/vm.log; nothing committed"; exit 3; }
grep -a -q "$OKRE" $X/offline/vm.log || { log "no $OKRE in offline/vm.log; nothing committed"; exit 3; }
grep -a "GFXALL" $X/offline/vm.log | cut -c1-240 | tee -a $X/events
cp $O $X/owned.json
python3 $T/check_update_catalog_fast.py --hfsx-read $MAIN/research/nord2/display-capabilities/hfsx-read.py --snapshot-receipt $X/snapshot/verified.json --update $X/offline --owned-json $X/owned.json --output $X/catalog.json > $X/catalog.log 2>&1; log catalog ok
ADDON=$(python3 -c "import json;print(json.load(open('$X/offline/verified.json'))['addon_sha256'])")
python3 $T/commit_root_delta_fast.py --main $MAIN --snapshot-receipt $X/snapshot/verified.json --update $X/offline --output $X/device-install --expected-addon-sha256 $ADDON --commit > $X/device-install.log 2>&1; log committed
$D/to_fastboot.sh 300; $D/flash_boot.sh $IMG; log kernel flashed $(sha256sum $IMG | cut -c1-16)
mkdir -p $D/runs/$2; python3 $W/tools/nord2-usb-console.py $D/runs/$2/ui-usb --wait 240 --read-seconds $READS > $D/runs/$2/usb.log 2>&1 &
CP=$!; for i in $(seq 1 $(( READS / 3 + 100 ))); do sleep 3; kill -0 $CP 2>/dev/null || break; done; wait $CP 2>/dev/null || true
log capture done
