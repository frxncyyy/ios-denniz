#!/bin/bash
# gfxall1/gfxall2: fake-root rehearsal of the integrated userland guests (from gfx100a-20261114/ship/rehearse-gfx100a.sh).
# from apps-20261104/rehearse-shader99b.sh. Fake root = the CURRENT mirror's bundle + receipt (shader99b), brought to webxr1 by
# webxr-20261114/guest-webxr1.sh first (the phone after the orchestrator's webxr1 transaction). unshare -r; nothing touches the
# mirror or a phone. Every write is checked against owned-gfxall.json; every refusal leaves the root byte-identical.
set -euo pipefail
A=$(cd "$(dirname "$0")" && pwd); B=$(cd "$A/../.." && pwd); BU=System/Library/Extensions/Nord2Metal.bundle; X=$B/webxr-20261114
H="python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img"
NEWN=e6f75db15fd14bcb8e19bd0f42fe8499d16d86685d9ea7e4a333a003a0f1e593; WXN=36f755ea01782f8424863c74c6c637580d6fffc23c9a85fee99e7d419190ec6d
WXL=9f263e6ff8255b193a20bebc8a9def367d2b577b4c0127fccbb7a5c07405207a; S99N=fe4ecc9f222b45bd125af2df052cf73f34288f11e97b8b0d9043d1d7a552340d
S99L=7c0c5abaf75e54da5bb245cf9d7472564edd2c36113ffdd046193a0766138a56
TOP=$B/.rehearse-gfxall; rm -rf "$TOP"; trap 'rm -rf "$TOP"' EXIT
run() { sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$1" | sed '1d;$d' | sed "s|R='/work/root'|R='$R'|; s|'/src/'|'$2/'|g" > "$F/$(basename $1).py"; unshare -r python3 "$F/$(basename $1).py"; }
st() { (cd "$R" && find . \( -type f -o -type d \) -printf '%p %m\n' | sort; find . -type f -exec sha256sum {} + | sort) | sha256sum | cut -c1-16; }
die() { echo "REHEARSAL FAILED: $*"; exit 1; }
h() { sha256sum < "$R/$BU/$1" | cut -c1-64; }
snap() { (cd "$R" && find . -mindepth 1 \( -type d -printf '%p directory\n' -o -type f -exec sha256sum {} + \) | sed 's|^\([0-9a-f]\{64\}\)  \(.*\)$|\2 \1|' | sort) > "$1"; }
owned() { python3 - "$A/owned-gfxall.json" "$1" "$2" <<'PY'
import json,sys
own=set(json.load(open(sys.argv[1])))
def load(p):
    d={}
    for l in open(p):
        k,v=l.rstrip('\n').rsplit(' ',1); d[k[1:]]=v
    return d
a,b=load(sys.argv[2]),load(sys.argv[3])
bad=sorted(k for k in set(a)|set(b) if a.get(k)!=b.get(k) and k not in own)
assert not bad, 'written outside owned-gfxall.json: %r'%bad
print('  owned check: %d path(s) differ, all owned: %s'%(len([k for k in set(a)|set(b) if a.get(k)!=b.get(k)]), sorted(k.rsplit('/',1)[1] for k in set(a)|set(b) if a.get(k)!=b.get(k))))
PY
}
setfile() { cp "$2" "$R/$BU/$1"; python3 - "$R" "$1" <<'PY'
import hashlib,json,sys
R,n=sys.argv[1:]; p=R+'/private/var/db/nord2-metal/receipt.json'; rc=json.load(open(p)); BU='System/Library/Extensions/Nord2Metal.bundle'
rc['inventory'][BU][n]=hashlib.sha256(open(R+'/'+BU+'/'+n,'rb').read()).hexdigest(); open(p,'w').write(json.dumps(rc,indent=2)+'\n')
PY
}
fresh() {   # fresh <name>: fake root = the mirror's bundle + receipt (shader99b), then webxr1 installed over it
  F=$TOP/$1; mkdir -p "$F/root"; R=$F/root; DBD=$R/private/var/db/nord2-metal
  mkdir -p "$R/$BU" "$DBD" "$R/usr/lib"
  MGD=private/var/containers/Shared/SystemGroup/systemgroup.com.apple.mobilegestaltcache/Library/Caches; mkdir -p "$R/$MGD"
  $H get /$MGD "$R/$MGD" com.apple.MobileGestalt.plist >/dev/null 2>&1 || true
  $H get /private/var/db/nord2-metal "$DBD" receipt.json Nord2Metal.pre-shader99b libredwoodair.dylib.pre-shader99b Nord2Metal.pre-gfxall libredwoodair.dylib.pre-gfxall no- trace-cc >/dev/null 2>&1 || true
  local NAMES; NAMES=$(python3 -c "import json;inv=json.load(open('$DBD/receipt.json'))['inventory']['$BU'];print(' '.join(k for k,v in inv.items() if v!='directory'))")
  $H get /$BU "$R/$BU" $NAMES >/dev/null 2>&1
  for d in $(python3 -c "import json;inv=json.load(open('$DBD/receipt.json'))['inventory']['$BU'];print(' '.join(k for k,v in inv.items() if v=='directory'))"); do mkdir -p "$R/$BU/$d"; done
  chmod 755 "$R/$BU/Nord2Metal"
  # (15 Nov: the mirror now carries gfxall2 -- the phone's state after the orchestrator's gfxall2 transaction: gfxall2's own undo brings the
  #  fake root back to shader99b, which every scenario below starts from; scenario 0 checks gfxall6 straight over the mirror's gfxall2)
  if [ "${MIRROR_AS_IS:-0}" = 0 ] && [ "$(h libredwoodair.dylib)" != $S99L ]; then
    # (the mirror follows the phone: whichever gfxallN record it carries, that payload's own undo brings the fake root back to shader99b)
    local rec; rec=$(python3 -c "import json;rc=json.load(open('$DBD/receipt.json'));print(' '.join(sorted((k for k in rc if k.startswith('gfxall') and k[6:].isdigit()), key=lambda k: int(k[6:]), reverse=True)))")
    for g in $rec; do run $A/guest-$g-undo.sh $A/payload-$g-undo | grep -qE "^${g^^}_UNDONE" || die "$g undo on the fake root"; done
  fi
  [ "${MIRROR_AS_IS:-0}" = 1 ] && return 0
  [ "$(h Nord2Metal)" = $S99N ] && [ "$(h libredwoodair.dylib)" = $S99L ] || die "mirror is not shader99b (fe4ecc9f + 7c0c5aba)"
  [ "${2:-webxr}" = webxr ] || return 0
  run $X/guest-webxr1.sh $X/payload-webxr1 | grep -qE '^WEBXR1_INSTALLED' || die "webxr1 did not install on the fake root"
  [ "$(h Nord2Metal)" = $WXN ] && [ "$(h libredwoodair.dylib)" = $WXL ] || die "not webxr1 after its guest"
}
refused() {  # refused <guest> <payload> <grep> <what>: must fail with that reason and change nothing
  local s0; s0=$(st)
  if run "$1" "$2" > "$F/r.log" 2>&1; then die "$4 accepted"; fi
  grep -q -- "$3" "$F/r.log" || die "$4 refused for another reason: $(grep -m1 Error "$F/r.log")"
  [ "$(st)" = "$s0" ] || die "$4: refused but the root changed"
  echo "  refused, root unchanged: $4"
}
Y=$B/y2s99c-perf/ship; U=$B/gpusched-20261114/userland/ship; GX=$B/gfx100a-20261114/ship
N1=e6f75db15fd14bcb8e19bd0f42fe8499d16d86685d9ea7e4a333a003a0f1e593; L1=366b0e1a2448fe51f7b79597360dd93682fcd0c87e6279514118a6a6b2745581
N2=abf0e80b8ff2e441cc5ddf07a69aa288418b436e4663903a719a81d1098df7c3; L2=f25ca5e4cbe7925f49abcfefbf45244e6e422e2d6015108ab699ec735f1e756a
N3=744e3fa95dff2e08030f63a22bc9ca252b0ac7b73cfb8ab30d5792a912d55235; L3=0fb23f5e10d462a469b31d3040947360f4e402c4b6f82d5df34d8b961eef6238
G3=$A/guest-gfxall3.sh; G3U=$A/guest-gfxall3-undo.sh; P3=$A/payload-gfxall3; P3U=$A/payload-gfxall3-undo
N4=2cb9a64b56d714cca5e3e315044ec9e193b500967daac953a1eb69f2ebe2bc63; L4=1cb5fd62647166ba8358960bdf21800d8c6a0d19abff3685982a699a83f276a6
G4=$A/guest-gfxall4.sh; G4U=$A/guest-gfxall4-undo.sh; P4=$A/payload-gfxall4; P4U=$A/payload-gfxall4-undo
N5=2cb9a64b56d714cca5e3e315044ec9e193b500967daac953a1eb69f2ebe2bc63; L5=71a88a19e7bf66d7af45cdb07f1beeb50486d18c3e8ba472942fec2d9f2d8d73
G5=$A/guest-gfxall5.sh; G5U=$A/guest-gfxall5-undo.sh; P5=$A/payload-gfxall5; P5U=$A/payload-gfxall5-undo
N6=2cb9a64b56d714cca5e3e315044ec9e193b500967daac953a1eb69f2ebe2bc63; L6=937911aa6af7fbba994dc0161c98c3f2b3c9d91346540a1ce8c152f1e9858b2e
G6=$A/guest-gfxall6.sh; G6U=$A/guest-gfxall6-undo.sh; P6=$A/payload-gfxall6; P6U=$A/payload-gfxall6-undo
N7=2cb9a64b56d714cca5e3e315044ec9e193b500967daac953a1eb69f2ebe2bc63; L7=ec36ec3eccffd88c47daf8f0c6969b92fc1a4e2a2bb1b73f04860a71f3e9793d
G7=$A/guest-gfxall7.sh; G7U=$A/guest-gfxall7-undo.sh; P7=$A/payload-gfxall7; P7U=$A/payload-gfxall7-undo
N8=$(sha256sum < $A/payload-gfxall8/Nord2Metal | cut -c1-64); L8=$L7
G8=$A/guest-gfxall8.sh; G8U=$A/guest-gfxall8-undo.sh; P8=$A/payload-gfxall8; P8U=$A/payload-gfxall8-undo
N9=$(sha256sum < $A/payload-gfxall9/Nord2Metal | cut -c1-64); L9=$L7
G9=$A/guest-gfxall9.sh; G9U=$A/guest-gfxall9-undo.sh; P9=$A/payload-gfxall9; P9U=$A/payload-gfxall9-undo
N10=$(sha256sum < $A/payload-gfxall10/Nord2Metal | cut -c1-64); L10=$L7
G10=$A/guest-gfxall10.sh; G10U=$A/guest-gfxall10-undo.sh; P10=$A/payload-gfxall10; P10U=$A/payload-gfxall10-undo
N11=$(sha256sum < $A/payload-gfxall11/Nord2Metal | cut -c1-64); L11=$L7
G11=$A/guest-gfxall11.sh; G11U=$A/guest-gfxall11-undo.sh; P11=$A/payload-gfxall11; P11U=$A/payload-gfxall11-undo
N12=$(sha256sum < $A/payload-gfxall12/Nord2Metal | cut -c1-64); L12=$L7
G12=$A/guest-gfxall12.sh; G12U=$A/guest-gfxall12-undo.sh; P12=$A/payload-gfxall12; P12U=$A/payload-gfxall12-undo
N13=$(sha256sum < $A/payload-gfxall13/Nord2Metal | cut -c1-64); L13=$L7
G13=$A/guest-gfxall13.sh; G13U=$A/guest-gfxall13-undo.sh; P13=$A/payload-gfxall13; P13U=$A/payload-gfxall13-undo
N14=$(sha256sum < $A/payload-gfxall14/Nord2Metal | cut -c1-64); L14=$L7
G14=$A/guest-gfxall14.sh; G14U=$A/guest-gfxall14-undo.sh; P14=$A/payload-gfxall14; P14U=$A/payload-gfxall14-undo
N15=$(sha256sum < $A/payload-gfxall15/Nord2Metal | cut -c1-64); L15=$L7
G15=$A/guest-gfxall15.sh; G15U=$A/guest-gfxall15-undo.sh; P15=$A/payload-gfxall15; P15U=$A/payload-gfxall15-undo
N16=$(sha256sum < $A/payload-gfxall16/Nord2Metal | cut -c1-64); L16=$L7
G16=$A/guest-gfxall16.sh; G16U=$A/guest-gfxall16-undo.sh; P16=$A/payload-gfxall16; P16U=$A/payload-gfxall16-undo
N17=$(sha256sum < $A/payload-gfxall17/Nord2Metal | cut -c1-64); L17=$(sha256sum < $A/payload-gfxall17/libredwoodair.dylib | cut -c1-64)
G17=$A/guest-gfxall17.sh; G17U=$A/guest-gfxall17-undo.sh; P17=$A/payload-gfxall17; P17U=$A/payload-gfxall17-undo
G1=$A/guest-gfxall1.sh; G1U=$A/guest-gfxall1-undo.sh; G2=$A/guest-gfxall2.sh; G2U=$A/guest-gfxall2-undo.sh
P1=$A/payload-gfxall1; P1N=$A/payload-gfxall1-nopipe; P1U=$A/payload-gfxall1-undo
P2=$A/payload-gfxall2; P2O=$A/payload-gfxall2-off; P2R=$A/payload-gfxall2-nortt; P2P=$A/payload-gfxall2-nopools; P2U=$A/payload-gfxall2-undo
cmp -s $P8/libredwoodair.dylib $A/../all8-b2/payload/Nord2Metal.bundle/libredwoodair.dylib && cmp -s $P8/Nord2Metal $A/../all8-b2/payload/Nord2Metal.bundle/Nord2Metal && [ $(sha256sum < $P8/libredwoodair.dylib | cut -c1-64) = $L7 ] || die "gfxall8 payload is not the build (or its library is not gfxall7's)"
cmp -s $P7/libredwoodair.dylib $A/../all7-b2/payload/Nord2Metal.bundle/libredwoodair.dylib || die "gfxall7 payload is not the build"
cmp -s $P6/libredwoodair.dylib $A/../all6-b2/payload/Nord2Metal.bundle/libredwoodair.dylib || die "gfxall6 payload is not the build"
cmp -s $P5/libredwoodair.dylib $A/../all5-b2/payload/Nord2Metal.bundle/libredwoodair.dylib || die "gfxall5 payload is not the build"
cmp -s $P4/libredwoodair.dylib $A/../all4-b2/payload/Nord2Metal.bundle/libredwoodair.dylib && cmp -s $P4/Nord2Metal $A/../all4-b2/payload/Nord2Metal.bundle/Nord2Metal || die "gfxall4 payload is not the build"
cmp -s $P3/libredwoodair.dylib $A/../all3-b2/payload/Nord2Metal.bundle/libredwoodair.dylib && cmp -s $P3/Nord2Metal $A/../all3-b2/payload/Nord2Metal.bundle/Nord2Metal || die "gfxall3 payload is not the build"
cmp -s $P1/libredwoodair.dylib $A/../all1-b2/payload/Nord2Metal.bundle/libredwoodair.dylib || die "gfxall1 payload is not the build"
cmp -s $P2/libredwoodair.dylib $A/../all2-b2/payload/Nord2Metal.bundle/libredwoodair.dylib && cmp -s $P2/Nord2Metal $A/../all2-b2/payload/Nord2Metal.bundle/Nord2Metal || die "gfxall2 payload is not the build"
cmp -s $P17/libredwoodair.dylib $A/../all17-b2/payload/Nord2Metal.bundle/libredwoodair.dylib && cmp -s $P17/Nord2Metal $A/../all17-b2/payload/Nord2Metal.bundle/Nord2Metal || die "gfxall17 payload is not the build"
pair() { [ "$(h Nord2Metal)" = "$1" ] && [ "$(h libredwoodair.dylib)" = "$2" ] || die "$3: pair $(h Nord2Metal | cut -c1-8) $(h libredwoodair.dylib | cut -c1-8)"; }

echo "== 0 the CURRENT mirror (as the phone left it)"
MIRROR_AS_IS=1 fresh m0; s0=$(st)
MGP0="$R/$MGD/com.apple.MobileGestalt.plist"; mg0=$(sha256sum < "$MGP0" 2>/dev/null || echo none)
case "$(h Nord2Metal) $(h libredwoodair.dylib)" in
"$N16 $L16")
  echo "== 0.17 mirror = gfxall16(-seeded): gfxall17-diag, -norot, -flip-seeded, plain, undo -> gfxall16 pair"
  run $G17 $A/payload-gfxall17-diag | grep -E "^GFXALL17_(INSTALLED|FLAG|LOGDIR)" | tr "\n" " " | cut -c1-400; echo; pair $N17 $L17 "gfxall17 over the mirror"
  run $G17 $A/payload-gfxall17-norot | grep -E "^GFXALL17_FLAG" | tr "\n" " " | cut -c1-300; echo; [ -e "$DBD/no-gfxall17-rot" ] || die "norot switch"
  run $G17 $A/payload-gfxall17-flip-seeded | grep -E "^GFXALL17_FLAG" | tr "\n" " " | cut -c1-300; echo; [ -e "$DBD/gfxall17-rot-flip" ] && [ ! -e "$DBD/no-gfxall17-rot" ] || die "flip switch"
  run $G17 $P17 >/dev/null; [ ! -e "$DBD/no-gfxall17-rot" ] && [ ! -e "$DBD/gfxall17-rot-flip" ] && [ ! -e "$DBD/surf-all" ] || die "plain kept a switch"
  run $G17U $P17U | grep -E "^GFXALL17_UNDONE" | cut -c1-200; pair $N16 $L16 "gfxall17 undo gives gfxall16";;
"$N15 $L15")
  echo "== 0.16 mirror = gfxall15(-diag): gfxall16-diag, -nob3a8, plain, undo -> gfxall15 pair"
  run $G16 $A/payload-gfxall16-diag | grep -E "^GFXALL16_(INSTALLED|FLAG|LOGDIR)" | tr "\n" " " | cut -c1-400; echo; pair $N16 $L16 "gfxall16 over the mirror"
  run $G16 $A/payload-gfxall16-nob3a8 | grep -E "^GFXALL16_FLAG" | tr "\n" " " | cut -c1-300; echo; [ -e "$DBD/no-b3a8" ] || die "nob3a8 switch"
  run $G16 $P16 >/dev/null; [ ! -e "$DBD/no-b3a8" ] && [ ! -e "$DBD/surf-all" ] || die "plain kept a switch"
  run $G16U $P16U | grep -E "^GFXALL16_UNDONE" | cut -c1-200; pair $N15 $L15 "gfxall16 undo gives gfxall15";;
"$N14 $L14")
  echo "== 0.15 mirror = gfxall14(-diag): gfxall15-diag, -nob3a8, plain, undo -> gfxall14 pair"
  run $G15 $A/payload-gfxall15-diag | grep -E "^GFXALL15_(INSTALLED|FLAG|LOGDIR)" | tr "\n" " " | cut -c1-400; echo; pair $N15 $L15 "gfxall15 over the mirror"
  run $G15 $A/payload-gfxall15-nob3a8 | grep -E "^GFXALL15_FLAG" | tr "\n" " " | cut -c1-300; echo; [ -e "$DBD/no-b3a8" ] || die "nob3a8 switch"
  run $G15 $P15 >/dev/null; [ ! -e "$DBD/no-b3a8" ] && [ ! -e "$DBD/surf-all" ] || die "plain kept a switch"
  run $G15U $P15U | grep -E "^GFXALL15_UNDONE" | cut -c1-200; pair $N14 $L14 "gfxall15 undo gives gfxall14";;
"$N13 $L13")
  echo "== 0.14 mirror = gfxall13(-seeded/-diag): gfxall14-diag, -nolimit, plain, undo -> gfxall13 pair"
  run $G14 $A/payload-gfxall14-diag | grep -E "^GFXALL14_(INSTALLED|FLAG|LOGDIR)" | tr "\n" " " | cut -c1-400; echo; pair $N14 $L14 "gfxall14 over the mirror"
  run $G14 $A/payload-gfxall14-nolimit | grep -E "^GFXALL14_FLAG" | tr "\n" " " | cut -c1-300; echo; [ -e "$DBD/no-msltimeout" ] || die "nolimit switch"
  run $G14 $P14 >/dev/null; [ ! -e "$DBD/no-msltimeout" ] && [ ! -e "$DBD/surf-all" ] || die "plain kept a switch"
  run $G14U $P14U | grep -E "^GFXALL14_UNDONE" | cut -c1-200; pair $N13 $L13 "gfxall14 undo gives gfxall13";;
"$N12 $L12")
  echo "== 0.13 mirror = gfxall12(-diag): gfxall13, -noxr, plain, undo -> gfxall12 pair"
  run $G13 $P13 | grep -E "^GFXALL13_(INSTALLED|FLAG|LOGDIR)" | tr "\n" " " | cut -c1-400; echo; pair $N13 $L13 "gfxall13 over the mirror"
  [ ! -e "$DBD/surf-all" ] || die "plain gfxall13 kept surf-all"
  run $G13 $A/payload-gfxall13-noxr | grep -E "^GFXALL13_FLAG" | tr "\n" " " | cut -c1-300; echo; [ -e "$DBD/no-xrsurf" ] || die "noxr switch"
  run $G13 $P13 >/dev/null; [ ! -e "$DBD/no-xrsurf" ] || die "plain kept no-xrsurf"
  run $G13U $P13U | grep -E "^GFXALL13_UNDONE" | cut -c1-200; pair $N12 $L12 "gfxall13 undo gives gfxall12";;
"$N11 $L11")
  echo "== 0.12 mirror = gfxall11(-seeded): gfxall12-diag, plain, undo -> gfxall11 pair; the read-only seed cache is kept"
  ro0=$( (ls "$R/usr/lib/nord2-metal-cache" 2>/dev/null || true) | wc -l)
  run $G12 $A/payload-gfxall12-diag | grep -E "^GFXALL12_(INSTALLED|FLAG|LOGDIR|ROCACHE)" | tr "\n" " " | cut -c1-400; echo; pair $N12 $L12 "gfxall12 over the mirror"
  [ -e "$DBD/surf-all" ] && [ -d "$DBD/log" ] || die "diag switches"; [ "$( (ls "$R/usr/lib/nord2-metal-cache" 2>/dev/null || true) | wc -l)" = "$ro0" ] || die "seed cache changed"
  run $G12 $P12 >/dev/null; [ ! -e "$DBD/surf-all" ] || die "plain kept surf-all"
  run $G12U $P12U | grep -E "^GFXALL12_UNDONE" | cut -c1-200; pair $N11 $L11 "gfxall12 undo gives gfxall11"
  [ "$( (ls "$R/usr/lib/nord2-metal-cache" 2>/dev/null || true) | wc -l)" = "$ro0" ] || die "undo touched gfxall11's seed cache";;
"$N10 $L10")
  echo "== 0.11 mirror = gfxall10 (a variant): gfxall11, -off, plain again, undo -> gfxall10 pair; diag switches cleared by gfxall11"
  run $G11 $P11 | grep -E "^GFXALL11_(INSTALLED|MG|FLAG|LOGDIR)" | tr "\n" " " | cut -c1-400; echo; pair $N11 $L11 "gfxall11 over the mirror"
  for f in surf-al trace-al matsnap-log no-matsnap mt-nodeep matsnap-allow; do [ ! -e "$DBD/$f" ] || die "gfxall11 left $f"; done; [ ! -e "$DBD/log" ] || die "gfxall11 left the log dir"
  run $G11 $A/payload-gfxall11-off | grep -E "^GFXALL11_FLAG" | tr "\n" " " | cut -c1-300; echo; [ -e "$DBD/matsnap-allow" ] || die "off without matsnap-allow"
  run $G11 $P11 >/dev/null; [ ! -e "$DBD/matsnap-allow" ] || die "plain kept matsnap-allow"
  run $G11U $P11U | grep -E "^GFXALL11_UNDONE" | cut -c1-200; pair $N10 $L10 "gfxall11 undo over the mirror gives gfxall10";;
"$N9 $L9")
  echo "== 0.10 mirror = gfxall9 (bgra: MobileGestalt key ours): gfxall10 + variants -> MG key taken back; undo -> gfxall9 pair"
  run $G10 $P10 | grep -E "^GFXALL10_(INSTALLED|MG|FLAG|LOGDIR)" | tr "\n" " " | cut -c1-400; echo; pair $N10 $L10 "gfxall10 over the mirror"
  python3 -c "import plistlib;d=plistlib.load(open('$MGP0','rb'));assert 'Aixt/MEN2O2B7f+8m4TxUA' not in d['CacheExtra']" || die "gfxall10 left gfxall9-bgra's key"
  for v in diag nosnap nodeep off; do run $G10 $A/payload-gfxall10-$v | grep -E "^GFXALL10_(FLAG|LOGDIR)" | tr "\n" " " | cut -c1-300; echo; done
  run $G10 $A/payload-gfxall10-nosnap >/dev/null; [ -e "$DBD/no-matsnap" ] && [ -e "$DBD/matsnap-log" ] || die "nosnap switches"
  run $G10U $P10U | grep -E "^GFXALL10_UNDONE" | cut -c1-200; pair $N9 $L9 "gfxall10 undo over the mirror gives gfxall9"
  [ ! -e "$DBD/no-matsnap" ] || die "undo kept no-matsnap";;
"$N8 $L8")
  echo "== 0.9 mirror = gfxall8 (a variant): gfxall9 + every variant + plain + undo -> gfxall8 pair, MobileGestalt cache byte-identical"
  run $G9 $P9 | grep -E "^GFXALL9_(INSTALLED|FLAG|LOGDIR)" | tr "\n" " " | cut -c1-300; echo; pair $N9 $L9 "gfxall9 over the mirror"
  for v in diag bgra off; do run $G9 $A/payload-gfxall9-$v | grep -E "^GFXALL9_(MG|FLAG|LOGDIR)" | tr "\n" " " | cut -c1-300; echo; done
  run $G9 $A/payload-gfxall9-bgra >/dev/null; run $G9U $P9U | grep -E "^GFXALL9_(UNDONE|MG)" | cut -c1-200; pair $N8 $L8 "gfxall9 undo over the mirror gives gfxall8"
  [ "$(sha256sum < "$MGP0" 2>/dev/null || echo none)" = "$mg0" ] || die "MobileGestalt cache differs from the mirror after bgra + undo";;
"$N7 $L7")
  echo "== 0.8 mirror = gfxall7: gfxall8 and gfxall9 over it, undo -> gfxall7 pair"
  run $G8 $P8 | grep -E "^GFXALL8_INSTALLED" | cut -c1-200; run $G8U $P8U >/dev/null; pair $N7 $L7 "gfxall8 undo over the mirror"
  run $G9 $A/payload-gfxall9-bgra | grep -E "^GFXALL9_(INSTALLED|MG)" | cut -c1-200; run $G9U $P9U >/dev/null; pair $N7 $L7 "gfxall9 undo over the mirror";;
*) echo "== 0.x mirror pair $(h Nord2Metal | cut -c1-8) $(h libredwoodair.dylib | cut -c1-8): gfxall9 over it, undo byte-identical"
  run $G9 $P9 | grep -E "^GFXALL9_INSTALLED" | cut -c1-200; run $G9U $P9U >/dev/null; [ "$(st)" = "$s0" ] || die "gfxall9 undo over the mirror";;
esac
for V in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16; do
  eval G=\$G$V GU=\$G${V}U P=\$P$V PU=\$P${V}U NN=\$N$V NL=\$L$V
  echo "== $V.a mirror state (shader99b): install, again, undo, again (gfxall$V)"
  fresh s$V noweb; s0=$(st); snap "$F/0.snap"
  run $G $P | grep -E "^GFXALL${V}_(INSTALLED|NOTE|FLAG)|^CC_"
  pair $NN $NL "gfxall$V installed"
  python3 -c "import json;rc=json.load(open('$DBD/receipt.json'));i=rc['inventory']['$BU'];r=rc['gfxall$V'];assert i['Nord2Metal']=='$NN' and i['libredwoodair.dylib']=='$NL' and r['previous']=={'Nord2Metal':'$S99N','libredwoodair.dylib':'$S99L'} and 'shader99b' in rc" || die "receipt"
  [ "$(stat -c %a "$R/$BU/Nord2Metal")" = 755 ] || die "mode"
  snap "$F/1.snap"; owned "$F/0.snap" "$F/1.snap"; s1=$(st)
  run $G $P | grep -qE "^GFXALL${V}_ALREADY" || die "not idempotent"; [ "$(st)" = "$s1" ] || die "second install changed the root"
  run $GU $PU | grep -E "^GFXALL${V}_UNDONE"
  [ "$(st)" = "$s0" ] || die "undo is not byte-identical to the start"
  run $GU $PU | grep -q "nothing to do" || die "second undo"; [ "$(st)" = "$s0" ] || die "second undo changed the root"
  echo "== $V.b from the other single-payload states (webxr1; webxr1+gfx100a; bigpool1; gpupipe1 on bigpool1): install + undo byte-identical"
  fresh w$V; s0=$(st); run $G $P | grep -E "^GFXALL${V}_INSTALLED" | cut -c1-200; pair $NN $NL "over webxr1"; run $GU $PU >/dev/null; [ "$(st)" = "$s0" ] || die "undo over webxr1"
  fresh x$V; run $GX/guest-gfx100a.sh $GX/payload-gfx100a | grep -qE '^GFX100A_INSTALLED' || die "gfx100a on fake root"; s0=$(st)
  run $G $P | grep -E "^GFXALL${V}_INSTALLED" | cut -c1-200; pair $NN $NL "over gfx100a"; snap "$F/2.snap"; run $GU $PU >/dev/null; [ "$(st)" = "$s0" ] || die "undo over gfx100a"
  fresh b$V noweb; run $Y/guest-bigpool1.sh $Y/payload-bigpool1 | grep -qE '^BIGPOOL1_INSTALLED' || die "bigpool1 on fake root"; s0=$(st)
  run $G $P | grep -E "^GFXALL${V}_INSTALLED" | cut -c1-200; pair $NN $NL "over bigpool1"; run $GU $PU >/dev/null; [ "$(st)" = "$s0" ] || die "undo over bigpool1"
  run $U/guest-gpupipe1.sh $U/payload-gpupipe1 | grep -qE '^GPUPIPE1_INSTALLED' || die "gpupipe1 on fake root"; s0=$(st)
  run $G $P | grep -E "^GFXALL${V}_INSTALLED" | cut -c1-200; pair $NN $NL "over gpupipe1"; run $GU $PU >/dev/null; [ "$(st)" = "$s0" ] || die "undo over gpupipe1"
  run $U/guest-gpupipe1-undo.sh $U/payload-gpupipe1-undo | grep -qE '^GPUPIPE1_UNDONE' || die "gpupipe1's own undo after ours"
done

echo "== 3 gfxall1 -> gfxall2 -> undo (back to gfxall1) -> undo gfxall1 = start"
fresh up noweb; s0=$(st); run $G1 $P1 >/dev/null; s1=$(st)
run $G2 $P2 | grep -E '^GFXALL2_INSTALLED' | cut -c1-220; pair $N2 $L2 "gfxall2 over gfxall1"
run $G2U $P2U | grep -E '^GFXALL2_UNDONE' | cut -c1-200; [ "$(st)" = "$s1" ] || die "gfxall2 undo is not gfxall1 again"
run $G1U $P1U >/dev/null; [ "$(st)" = "$s0" ] || die "gfxall1 undo after"

echo "== 4 switches: variants set theirs, the plain payload clears them, a foreign one is kept, undo removes ours"
fresh sw noweb; s0=$(st); snap "$F/0.snap"
run $G2 $P2O | grep -E '^GFXALL2_(INSTALLED|FLAG)' | cut -c1-120; [ "$(cat "$DBD/no-gfx200")" = made-by-gfxall ] || die "off switch"
run $G2 $P2R | grep -E '^GFXALL2_(ALREADY|FLAG)'; [ ! -e "$DBD/no-gfx200" ] && [ "$(cat "$DBD/no-gfx200-msaartt")" = made-by-gfxall ] || die "nortt"
run $G2 $P2P | grep -E '^GFXALL2_(ALREADY|FLAG)'; [ ! -e "$DBD/no-gfx200-msaartt" ] && [ -e "$DBD/no-gfx200-pools" ] && [ -e "$DBD/no-gfx200-rtpool" ] || die "nopools"
snap "$F/4.snap"; owned "$F/0.snap" "$F/4.snap"
run $G2 $P2 | grep -E '^GFXALL2_(ALREADY|FLAG)'; [ ! -e "$DBD/no-gfx200-pools" ] || die "not cleared"
printf 'by hand\n' > "$DBD/no-gpupipe"; run $G2 $P2 | grep -qE '^GFXALL2_NOTE no-gpupipe' || die "foreign switch not reported"
run $G2 $P2O >/dev/null; run $G2U $P2U | grep -E '^GFXALL2_(UNDONE|FLAG)' | cut -c1-160
[ "$(cat "$DBD/no-gpupipe")" = "by hand" ] || die "foreign switch touched"; rm "$DBD/no-gpupipe"; [ "$(st)" = "$s0" ] || die "undo after switches"
run $G1 $A/payload-gfxall1-nopipe | grep -E '^GFXALL1_FLAG'; [ "$(cat "$DBD/no-gpupipe")" = made-by-gfxall ] || die "nopipe"; run $G1U $P1U >/dev/null; [ "$(st)" = "$s0" ] || die "gfxall1 nopipe undo"

echo "== 5 undo without backups uses the payload's shader99b copies; a damaged backup refuses"
run $G2 $P2 >/dev/null; rm "$DBD/Nord2Metal.pre-gfxall2" "$DBD/libredwoodair.dylib.pre-gfxall2"
run $G2U $P2U | grep -E '^GFXALL2_UNDONE' | grep -q fallback || die "fallback"; [ "$(st)" = "$s0" ] || die "fallback undo"
run $G2 $P2 >/dev/null; printf 'x' >> "$DBD/libredwoodair.dylib.pre-gfxall2"
refused $G2U $P2U "no usable copy" "undo with a damaged backup"
cp $P2U/libredwoodair.dylib "$DBD/libredwoodair.dylib.pre-gfxall2"; run $G2U $P2U >/dev/null; [ "$(st)" = "$s0" ] || die "undo after repair"

echo "== 6 refusals (root unchanged)"
fresh r1 noweb; setfile Nord2Metal "$B/apps-20261104/payload-gpufinal7/Nord2Metal"; refused $G2 $P2 "is not a state" "Nord2Metal = gpufinal7"
fresh r2 noweb; setfile libredwoodair.dylib $P2/libredwoodair.dylib; refused $G2 $P2 "installed without its record" "gfxall2 library without record"
fresh r3 noweb; printf 'x' >> "$R/$BU/libredwoodair.dylib"; refused $G2 $P2 "differs from its receipt" "bundle differs from receipt"
fresh r4 noweb; python3 -c "import json;p='$DBD/receipt.json';rc=json.load(open(p));rc['gfxall2']={'installed':'x'};open(p,'w').write(json.dumps(rc,indent=2)+'\n')"; refused $G2 $P2 "stale receipt record" "stale record"
fresh r5 noweb; printf 'o\n' > "$DBD/Nord2Metal.pre-gfxall2"; refused $G2 $P2 "a different backup" "different backup"
fresh r6 noweb; ln -s /etc/passwd "$DBD/no-gfx200"; refused $G2 $P2 "not a regular file" "switch symlink"
fresh r7 noweb; mkdir "$F/bad"; cp $P2/* "$F/bad/"; printf 'x' >> "$F/bad/Nord2Metal"; refused $G2 "$F/bad" "is not the gfxall2 one" "damaged payload"
fresh r8 noweb; setfile libredwoodair.dylib "$B/apps-20261104/payload-gpufinal7/libredwoodair.dylib" 2>/dev/null && refused $G2U $P2U "neither gfxall2" "undo over a foreign library" || true
echo "== 7 gfxall3: cache directory made 1777, runtime cache files go with the undo; an existing one is kept; gfxall2 -> gfxall3 -> undo = gfxall2"
fresh c3 noweb; s0=$(st); run $G3 $P3 | grep -E '^GFXALL3_(INSTALLED|CACHEDIR)' | cut -c1-160
[ "$(stat -c %a "$DBD/cache")" = 1777 ] || die "cache dir mode"
printf 'x' > "$DBD/cache/0123.pc"; printf 'y' > "$DBD/cache/4567.pc.99.tmp"
run $G3U $P3U | grep -E '^GFXALL3_UNDONE' | cut -c1-220; [ "$(st)" = "$s0" ] || die "gfxall3 undo with cache files is not byte-identical"
mkdir -m 755 "$DBD/cache"; printf 'old\n' > "$DBD/cache/keep.pc"; sk=$(st)
run $G3 $P3 >/dev/null; run $G3U $P3U >/dev/null; [ "$(st)" = "$sk" ] && [ "$(cat "$DBD/cache/keep.pc")" = old ] || die "existing cache dir touched"
rm -r "$DBD/cache"; [ "$(st)" = "$s0" ] || die "cleanup"
run $G2 $P2 >/dev/null; s2=$(st); run $G3 $P3 | grep -E '^GFXALL3_INSTALLED' | cut -c1-200; pair $N3 $L3 "gfxall3 over gfxall2"
run $G3U $P3U >/dev/null; [ "$(st)" = "$s2" ] || die "gfxall3 undo is not gfxall2 again"; run $G2U $P2U >/dev/null; [ "$(st)" = "$s0" ] || die "chain end"
for v in off nocache nostate; do run $G3 $A/payload-gfxall3-$v | grep -E '^GFXALL3_FLAG' | tr '\n' ' '; echo; done
snap "$F/7.snap"; run $G3U $P3U >/dev/null; [ "$(st)" = "$s0" ] || die "undo after variants"
fresh c3l noweb; ln -s /tmp "$DBD/cache"; refused $G3 $P3 "cache path is not a directory" "a cache path that is a symlink"
echo "== 8 gfxall4: read-only seed shipped in pancache-seed/ lands in /usr/lib/nord2-metal-cache and goes with the undo; bad seeds refused"
fresh s4 noweb; s0=$(st); mkdir -p "$F/seedpay"; cp $P4/* "$F/seedpay/"; mkdir "$F/seedpay/pancache-seed"
python3 - "$F/seedpay/pancache-seed" <<'PY'
import hashlib, struct, sys, os
for i in range(3):
    payload = os.urandom(200 + i)
    key = hashlib.sha1(b'k%d' % i).digest()
    h = struct.pack('<II', 0x4350324e, 1) + key + struct.pack('<I', len(payload)) + hashlib.sha1(payload).digest()
    open(os.path.join(sys.argv[1], key.hex() + '.pc'), 'wb').write(h + payload)
PY
run $G4 "$F/seedpay" | grep -E '^GFXALL4_(INSTALLED|ROCACHE|CACHEDIR)' | cut -c1-160
[ "$(ls "$R/usr/lib/nord2-metal-cache" | wc -l)" = 3 ] && [ "$(stat -c %a "$R/usr/lib/nord2-metal-cache")" = 755 ] || die "seed not installed"
cmp -s "$F/seedpay/pancache-seed/$(ls "$F/seedpay/pancache-seed" | head -1)" "$R/usr/lib/nord2-metal-cache/$(ls "$F/seedpay/pancache-seed" | head -1)" || die "seed content"
run $G4U $P4U | grep -E '^GFXALL4_UNDONE' | cut -c1-220; [ "$(st)" = "$s0" ] || die "seeded undo is not byte-identical"
head -c 100 /dev/zero > "$F/seedpay/pancache-seed/$(printf 'f%.0s' $(seq 40)).pc"; refused $G4 "$F/seedpay" "seed magic" "a seed file that is not an entry"
rm "$F/seedpay/pancache-seed/"ffff*; printf 'x' > "$F/seedpay/pancache-seed/notahash.pc"; refused $G4 "$F/seedpay" "seed file name" "a seed file with a foreign name"
rm "$F/seedpay/pancache-seed/notahash.pc"
mkdir -p "$R/usr/lib/nord2-metal-cache"; printf 'mine' > "$R/usr/lib/nord2-metal-cache/other.txt"; sk=$(st)
run $G4 "$F/seedpay" > /dev/null; run $G4U $P4U >/dev/null; [ "$(st)" = "$sk" ] || die "existing read-only dir with foreign file touched"
rm -r "$R/usr/lib/nord2-metal-cache"; [ "$(st)" = "$s0" ] || die "cleanup 8"
run $G3 $P3 >/dev/null; s3=$(st); run $G4 $P4 | grep -E '^GFXALL4_INSTALLED' | cut -c1-200; pair $N4 $L4 "gfxall4 over gfxall3"
run $G4U $P4U >/dev/null; [ "$(st)" = "$s3" ] || die "gfxall4 undo is not gfxall3 again"; run $G3U $P3U >/dev/null; [ "$(st)" = "$s0" ] || die "chain end 8"
echo "== 9 gfxall5: over gfxall4 (same Nord2Metal: library only) -> undo = gfxall4; variants; seed path as gfxall4"
fresh f5 noweb; s0=$(st); run $G4 $P4 >/dev/null; s4=$(st)
run $G5 $P5 | grep -E '^GFXALL5_INSTALLED' | cut -c1-200; pair $N5 $L5 "gfxall5 over gfxall4"
run $G5 $P5 | grep -qE '^GFXALL5_ALREADY' || die "gfxall5 not idempotent"
run $G5U $P5U >/dev/null; [ "$(st)" = "$s4" ] || die "gfxall5 undo is not gfxall4 again"; run $G4U $P4U >/dev/null; [ "$(st)" = "$s0" ] || die "chain end 9"
for v in nov4 nopush off; do run $G5 $A/payload-gfxall5-$v | grep -E '^GFXALL5_FLAG' | tr '\n' ' '; echo; done
run $G5U $P5U >/dev/null; [ "$(st)" = "$s0" ] || die "undo after gfxall5 variants"
mkdir -p "$F/seedpay5"; cp $P5/* "$F/seedpay5/"; mkdir "$F/seedpay5/pancache-seed"
python3 - "$F/seedpay5/pancache-seed" <<'PY'
import hashlib, struct, sys, os
payload = os.urandom(300); key = hashlib.sha1(b'g5').digest()
open(os.path.join(sys.argv[1], key.hex() + '.pc'), 'wb').write(struct.pack('<II', 0x4350324e, 1) + key + struct.pack('<I', len(payload)) + hashlib.sha1(payload).digest() + payload)
PY
run $G5 "$F/seedpay5" | grep -E '^GFXALL5_ROCACHE'; run $G5U $P5U >/dev/null; [ "$(st)" = "$s0" ] || die "gfxall5 seeded undo"
echo "== 10 gfxall6 (fix): over gfxall5 (library only) -> undo = gfxall5; over gfxall2; variants"
fresh f6 noweb; s0=$(st); run $G5 $P5 >/dev/null; s5=$(st)
run $G6 $P6 | grep -E '^GFXALL6_INSTALLED' | cut -c1-200; pair $N6 $L6 "gfxall6 over gfxall5"
run $G6U $P6U >/dev/null; [ "$(st)" = "$s5" ] || die "gfxall6 undo is not gfxall5 again"; run $G5U $P5U >/dev/null; [ "$(st)" = "$s0" ] || die "chain end 10"
run $G2 $P2 >/dev/null; s2=$(st); run $G6 $P6 | grep -E '^GFXALL6_INSTALLED' | cut -c1-200; run $G6U $P6U >/dev/null; [ "$(st)" = "$s2" ] || die "gfxall6 over gfxall2 undo"; run $G2U $P2U >/dev/null
for v in nopipe nov4 off; do run $G6 $A/payload-gfxall6-$v | grep -E '^GFXALL6_FLAG' | tr '\n' ' '; echo; done
run $G6U $P6U >/dev/null; [ "$(st)" = "$s0" ] || die "undo after gfxall6 variants"
echo "== 11 gfxall7: over gfxall6 (library only) -> undo = gfxall6; variants"
fresh f7 noweb; s0=$(st); run $G6 $P6 >/dev/null; s6=$(st)
run $G7 $P7 | grep -E '^GFXALL7_INSTALLED' | cut -c1-200; pair $N7 $L7 "gfxall7 over gfxall6"
run $G7U $P7U >/dev/null; [ "$(st)" = "$s6" ] || die "gfxall7 undo is not gfxall6 again"; run $G6U $P6U >/dev/null; [ "$(st)" = "$s0" ] || die "chain end 11"
for v in nopipe nov4 off; do run $G7 $A/payload-gfxall7-$v | grep -E '^GFXALL7_FLAG' | tr '\n' ' '; echo; done
run $G7U $P7U >/dev/null; [ "$(st)" = "$s0" ] || die "undo after gfxall7 variants"
echo "== 12 gfxall7-diag: trace-cc + per-process log directory; the plain payload takes both away; undo with logs; a foreign log dir is kept"
fresh d7 noweb; s0=$(st); run $G7 $A/payload-gfxall7-diag | grep -E '^GFXALL7_(INSTALLED|LOGDIR|FLAG)' | cut -c1-160
[ "$(stat -c %a "$DBD/log")" = 1777 ] && [ -e "$DBD/trace-cc" ] || die "diag not installed"
printf 'x\n' > "$DBD/log/backboardd.61.log"
run $G7 $P7 | grep -E '^GFXALL7_(ALREADY|LOGDIR|FLAG)'; [ ! -e "$DBD/log" ] && [ ! -e "$DBD/trace-cc" ] || die "plain payload kept diag"
run $G7 $A/payload-gfxall7-diag > /dev/null; printf 'y\n' > "$DBD/log/SpringBoard.26.log"
run $G7U $P7U | grep -E '^GFXALL7_(UNDONE|LOGDIR)' | cut -c1-160; [ "$(st)" = "$s0" ] || die "diag undo not byte-identical"
mkdir -m 755 "$DBD/log"; printf 'old\n' > "$DBD/log/keep.log"; sk=$(st)
run $G7 $A/payload-gfxall7-diag > /dev/null; run $G7U $P7U > /dev/null; [ "$(st)" = "$sk" ] || die "foreign log dir touched"
rm -r "$DBD/log"; [ "$(st)" = "$s0" ] || die "cleanup 12"
echo "== 13 gfxall8: over gfxall7 (Nord2Metal only) -> undo = gfxall7; diag variants own trace-al / no-rt1 / no-singletarget / log; plain clears"
fresh f8 noweb; s0=$(st); run $G7 $P7 >/dev/null; s7=$(st)
run $G8 $P8 | grep -E '^GFXALL8_INSTALLED' | cut -c1-200; pair $N8 $L8 "gfxall8 over gfxall7"
run $G8U $P8U >/dev/null; [ "$(st)" = "$s7" ] || die "gfxall8 undo is not gfxall7 again"
run $G7 $A/payload-gfxall7-diag >/dev/null; s7d=$(st); run $G8 $A/payload-gfxall8-diag-nofb | grep -E '^GFXALL8_(INSTALLED|FLAG|LOGDIR)' | tr '\n' ' ' | cut -c1-300; echo
[ -e "$DBD/trace-al" ] && [ -e "$DBD/no-rt1" ] && [ -e "$DBD/no-singletarget" ] && [ ! -e "$DBD/trace-cc" ] && [ -d "$DBD/log" ] || die "diag-nofb switches"
printf 'z\n' > "$DBD/log/backboardd.61.log"; snap "$F/8.snap"
run $G8 $P8 | grep -E '^GFXALL8_(ALREADY|FLAG|LOGDIR)' | tr '\n' ' ' | cut -c1-300; echo
[ ! -e "$DBD/trace-al" ] && [ ! -e "$DBD/no-rt1" ] && [ ! -e "$DBD/no-singletarget" ] && [ ! -e "$DBD/log" ] || die "plain gfxall8 kept a diag switch"
run $G8 $A/payload-gfxall8-diag-off | grep -cE '^GFXALL8_FLAG' ; run $G8U $P8U >/dev/null; run $G7U $P7U >/dev/null; [ "$(st)" = "$s0" ] || die "chain end 13"
printf 'by hand\n' > "$DBD/no-rt1"; sf=$(st); run $G8 $A/payload-gfxall8-diag-nofb > /dev/null; [ "$(cat "$DBD/no-rt1")" = "by hand" ] || die "foreign no-rt1 overwritten"
run $G8U $P8U >/dev/null; [ "$(st)" = "$sf" ] || die "undo with a foreign no-rt1"; rm "$DBD/no-rt1"; [ "$(st)" = "$s0" ] || die "cleanup 13"
echo "== 14 gfxall9: over gfxall8 -> undo = gfxall8; gfxall9-bgra writes MobileGestalt's deep-colour answer NO and every other variant / the undo takes it back"
fresh f9 noweb; MGP="$R/$MGD/com.apple.MobileGestalt.plist"; [ -f "$MGP" ] || die "no MobileGestalt cache in the fake root"; s0=$(st); m0=$(sha256sum < "$MGP")
run $G8 $P8 >/dev/null; s8=$(st)
run $G9 $P9 | grep -E '^GFXALL9_INSTALLED' | cut -c1-200; pair $N9 $L9 "gfxall9 over gfxall8"; run $G9U $P9U >/dev/null; [ "$(st)" = "$s8" ] || die "gfxall9 undo is not gfxall8"
snap "$F/9a.snap"; run $G9 $A/payload-gfxall9-bgra | grep -E '^GFXALL9_(INSTALLED|MG|FLAG|LOGDIR)' | cut -c1-200; snap "$F/9b.snap"; owned "$F/9a.snap" "$F/9b.snap"
python3 -c "import plistlib;d=plistlib.load(open('$MGP','rb'));assert d['CacheExtra']['Aixt/MEN2O2B7f+8m4TxUA'] is False" || die "deep-colour key not written"
[ "$(stat -c '%u %g %a' "$MGP")" = "$(cd $TOP/f9 && true; stat -c '%u %g %a' "$MGP")" ] || true
run $G9 $A/payload-gfxall9-bgra | grep -E '^GFXALL9_MG' | cut -c1-120
run $G9 $A/payload-gfxall9-diag | grep -E '^GFXALL9_MG' | cut -c1-160; [ "$(sha256sum < "$MGP")" = "$m0" ] || die "MobileGestalt cache not byte-identical after diag"
run $G9 $A/payload-gfxall9-bgra >/dev/null; run $G9U $P9U | grep -E '^GFXALL9_(UNDONE|MG)' | cut -c1-160; [ "$(st)" = "$s8" ] || die "bgra undo is not gfxall8"
run $G9 $A/payload-gfxall9-bgra >/dev/null
python3 - "$MGP" <<'PY'
import plistlib,sys
d=plistlib.load(open(sys.argv[1],'rb')); d['CacheExtra']['zzRehearsalOtherKey']=7; open(sys.argv[1],'wb').write(plistlib.dumps(d,fmt=plistlib.FMT_BINARY))
PY
run $G9 $P9 | grep -E '^GFXALL9_MG' | cut -c1-160
python3 -c "import plistlib;d=plistlib.load(open('$MGP','rb'));e=d['CacheExtra'];assert 'Aixt/MEN2O2B7f+8m4TxUA' not in e and e['zzRehearsalOtherKey']==7" || die "changed cache: our key not removed alone"
run $G9U $P9U >/dev/null; run $G8U $P8U >/dev/null
python3 - "$MGP" <<'PY'
import plistlib,sys
d=plistlib.load(open(sys.argv[1],'rb')); d['CacheExtra'].pop('zzRehearsalOtherKey'); open(sys.argv[1],'wb').write(plistlib.dumps(d,fmt=plistlib.FMT_BINARY))
PY
python3 - "$MGP" <<'PY'
import plistlib,sys
d=plistlib.load(open(sys.argv[1],'rb')); d['CacheExtra']['Aixt/MEN2O2B7f+8m4TxUA']=True; open(sys.argv[1],'wb').write(plistlib.dumps(d,fmt=plistlib.FMT_BINARY))
PY
sk=$(st); run $G9 $A/payload-gfxall9-bgra | grep -E '^GFXALL9_NOTE' | cut -c1-160; run $G9U $P9U >/dev/null; [ "$(st)" = "$sk" ] || die "a foreign deep-colour answer was touched"
echo "== 15 seeded payloads (gfxall8-seeded, gfxall9-seeded): install over the mirror state, read-only cache in place, undo byte-identical"
for SV in gfxall8 gfxall9 gfxall11 gfxall13 gfxall14 gfxall15 gfxall16 gfxall17; do
  SP=$A/payload-$SV-seeded; [ -d "$SP" ] || { echo "  (no payload-$SV-seeded: skipped)"; continue; }
  fresh se$SV noweb; s0=$(st); snap "$F/s0.snap"
  run $A/guest-$SV.sh $SP | grep -E "^${SV^^}_(INSTALLED|ROCACHE)" | cut -c1-160
  n=$(ls "$SP/pancache-seed" | wc -l); [ "$(ls "$R/usr/lib/nord2-metal-cache" | wc -l)" = "$n" ] || die "$SV seed not installed"
  snap "$F/s1.snap"; python3 - "$A/owned-$SV-seeded.json" "$F/s0.snap" "$F/s1.snap" <<'PY'
import json,sys
own=set(json.load(open(sys.argv[1])))
def load(p):
    d={}
    for l in open(p):
        k,v=l.rstrip('\n').rsplit(' ',1); d[k[1:]]=v
    return d
a,b=load(sys.argv[2]),load(sys.argv[3]); bad=sorted(k for k in set(a)|set(b) if a.get(k)!=b.get(k) and k not in own)
assert not bad, bad[:5]; print('  owned (seeded list) check: %d paths differ, all owned'%len([k for k in set(a)|set(b) if a.get(k)!=b.get(k)]))
PY
  run $A/guest-$SV-undo.sh $A/payload-$SV-undo | grep -E "^${SV^^}_UNDONE" | cut -c1-160; [ "$(st)" = "$s0" ] || die "$SV seeded undo"
done
echo "== 16 gfxall10: over gfxall9-bgra -> MG key back, undo = gfxall9 binaries; nosnap/diag switches set and cleared"
fresh f10 noweb; MGP="$R/$MGD/com.apple.MobileGestalt.plist"; s0=$(st); m0=$(sha256sum < "$MGP")
run $G9 $A/payload-gfxall9-bgra >/dev/null; run $G10 $A/payload-gfxall10-nosnap | grep -E '^GFXALL10_(INSTALLED|MG|FLAG)' | tr '\n' ' ' | cut -c1-400; echo
[ "$(sha256sum < "$MGP")" = "$m0" ] || die "MG not byte-identical after gfxall10 over gfxall9-bgra"
run $G10 $P10 | grep -E '^GFXALL10_(ALREADY|FLAG|LOGDIR)' | tr '\n' ' ' | cut -c1-300; echo; [ ! -e "$DBD/no-matsnap" ] && [ ! -e "$DBD/matsnap-log" ] || die "plain kept matsnap switches"
run $G10U $P10U >/dev/null; pair $N9 $L9 "gfxall10 undo gives gfxall9"; run $G9U $P9U >/dev/null; [ "$(st)" = "$s0" ] || die "chain end 16"
echo REHEARSAL_OK
