#!/bin/bash
# SPDX-License-Identifier: MIT
#
# stage.sh TAG [OUTDIR] -- makes the gpu/hwcodec install payload under /tmp.  The main session runs the payload's
# install.py inside the phone's root (live or mounted); this script never touches a device and writes only under OUTDIR
# (default /tmp/n2hw-ip/stage).
#
#   IOS_DIR=${IOS_DIR:-/tmp/n2hw-ip/ios}   products from build_ios.sh
#   --partial                              stage what exists even without vcodecd (dry run of the layout)
#   --host-test                            after staging, apply install.py + undo.py to a fake root under OUTDIR/root
#                                          and check the result; this is the only test of the installer
#
# The payload contains: vcodecd, n2vcd_cli (if built) and rwjpeg_tool (if built), the launchd plist, install.py, undo.py,
# plan.json.  The five vendor objects are NOT here: they are the user's own phone files and the main session copies them
# from the phone's /vendor into /private/var/db/nord2-vcodec/vendor.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)          # gpu/hwcodec/install
W=$(cd "$HERE/.." && pwd)                    # gpu/hwcodec
TAG=${1:-dev}
shift || true
OUT=/tmp/n2hw-ip/stage
IOS_DIR=${IOS_DIR:-/tmp/n2hw-ip/ios}
PARTIAL=0
HOST_TEST=0
for a in "$@"; do
	case $a in
		--partial) PARTIAL=1 ;;
		--host-test) HOST_TEST=1 ;;
		--*) echo "stage.sh: unknown option $a" >&2; exit 2 ;;
		*) OUT=$a ;;
	esac
done

OUT_REAL=$(realpath -m "$OUT")
case $OUT_REAL in
	/tmp/?*) ;;
	*) echo "stage.sh: OUT must resolve under /tmp (got $OUT -> $OUT_REAL)" >&2; exit 2 ;;
esac
OUT=$OUT_REAL
rm -rf "$OUT"
mkdir -p "$OUT/payload/usr/local/bin" "$OUT/payload/usr/local/libexec" \
         "$OUT/payload/System/Library/LaunchDaemons"

copy_if() {
	local src=$1 dst=$2 mode=$3
	[ -f "$src" ] || return 1
	cp "$src" "$OUT/payload/$dst"
	chmod "$mode" "$OUT/payload/$dst"
}

BINARIES=()
HAVE_DAEMON=1
if copy_if "$IOS_DIR/vcodecd" "usr/local/bin/vcodecd" 0755; then
	BINARIES+=("usr/local/bin/vcodecd")
else
	HAVE_DAEMON=0
	if [ "$PARTIAL" -eq 0 ]; then
		echo "stage.sh: $IOS_DIR/vcodecd is missing (build_ios.sh could not link it: IV's iOS archive is not there yet)." >&2
		echo "stage.sh: rerun with --partial to stage the layout without it." >&2
		exit 1
	fi
	echo "stage.sh: WARNING vcodecd is not in the payload (partial stage)"
fi
copy_if "$IOS_DIR/n2vcd_cli" "usr/local/bin/n2vcd_cli" 0755 && BINARIES+=("usr/local/bin/n2vcd_cli") || true
copy_if "$IOS_DIR/rwjpeg_tool" "usr/local/bin/rwjpeg_tool" 0755 && BINARIES+=("usr/local/bin/rwjpeg_tool") || true
cp "$HERE/com.nord2.vcodecd.plist" "$OUT/payload/System/Library/LaunchDaemons/com.nord2.vcodecd.plist"
chmod 0644 "$OUT/payload/System/Library/LaunchDaemons/com.nord2.vcodecd.plist"
cp "$HERE/install.py" "$OUT/payload/usr/local/libexec/nord2-vcodec-install.py"
cp "$HERE/undo.py" "$OUT/payload/usr/local/libexec/nord2-vcodec-undo.py"
chmod 0755 "$OUT/payload/usr/local/libexec/nord2-vcodec-install.py" "$OUT/payload/usr/local/libexec/nord2-vcodec-undo.py"

python3 - "$OUT/payload" "$TAG" "$HAVE_DAEMON" "$HOST_TEST" <<'PY'
import hashlib, json, os, sys
payload, tag, have_daemon, host_test = sys.argv[1], sys.argv[2], sys.argv[3] == '1', sys.argv[4] == '1'
files = {}
def add(rel, mode):
    full = os.path.join(payload, rel)
    files['/' + rel] = {'payload': rel, 'mode': mode,
                        'sha256': hashlib.sha256(open(full, 'rb').read()).hexdigest()}
for rel, mode in (('usr/local/bin/vcodecd', 0o755), ('usr/local/bin/n2vcd_cli', 0o755),
                  ('usr/local/bin/rwjpeg_tool', 0o755),
                  ('System/Library/LaunchDaemons/com.nord2.vcodecd.plist', 0o644),
                  ('usr/local/libexec/nord2-vcodec-install.py', 0o755),
                  ('usr/local/libexec/nord2-vcodec-undo.py', 0o755)):
    if os.path.isfile(os.path.join(payload, rel)):
        add(rel, mode)
plan = {'format': 1, 'tag': tag, 'daemon': have_daemon, 'host_test': host_test, 'files': files,
        'undo': '/usr/local/libexec/nord2-vcodec-undo.py /'}
json.dump(plan, open(os.path.join(payload, 'plan.json'), 'w'), indent=1)
print('stage: %d files, daemon=%s tag=%s' % (len(files), have_daemon, tag))
PY
cp "$OUT/payload/plan.json" "$OUT/plan.json"

cat > "$OUT/README.txt" <<'EOF'
gpu/hwcodec install payload (made by install/stage.sh; the main session installs it, no worker touches the phone).

Root file system layout (see LAYOUT.md for the full table):
  /usr/local/bin/vcodecd                                     the daemon (minor 0 of /dev/redwood-vcodec)
  /usr/local/bin/n2vcd_cli                                   P03 client tool, when built
  /usr/local/bin/rwjpeg_tool                                 J02 hardware-JPEG tool, when built
  /System/Library/LaunchDaemons/com.nord2.vcodecd.plist      launchd job, RunAtLoad/KeepAlive false
  /private/var/db/nord2-vcodec/vendor/                       five vendor objects, copied by the main session
  /private/var/db/nord2-vcodec/install.json                  receipt for undo
  /var/log/nord2-vcodecd.log                                 daemon log (stderr/stdout)

On the phone / mounted root (run the installer from the payload on the PC; do NOT pre-copy the payload over the
root, the installer needs the payload as its source):
  ROOT=/path/to/mounted/root
  python3 payload/usr/local/libexec/nord2-vcodec-install.py "$ROOT" --payload payload
  # then put the five files from the phone's own /vendor in $ROOT/private/var/db/nord2-vcodec/vendor/bin and vendor/lib and run it again
  launchctl bootstrap system /System/Library/LaunchDaemons/com.nord2.vcodecd.plist   # on the live phone, once
  launchctl kickstart -k system/com.nord2.vcodecd
  tail -f /var/log/nord2-vcodecd.log                  # expect "N2VCD ... start" and later "session ... open"

Undo:
  launchctl bootout system/com.nord2.vcodecd
  python3 payload/usr/local/libexec/nord2-vcodec-undo.py "$ROOT" [--purge-vendor]

Never copy a vendor object into git; they are the user's own files and are not part of this payload.
EOF

echo "stage: payload at $OUT/payload"
find "$OUT/payload" -type f | sort | while read -r f; do
	printf '   %8s  %s\n' "$(stat -c %s "$f")" "${f#"$OUT/payload"/}"
done

if [ "$HOST_TEST" -eq 1 ]; then
	ROOT=$OUT/root
	rm -rf "$ROOT"
	mkdir -p "$ROOT/usr/local/bin" "$ROOT/System/Library/LaunchDaemons" "$ROOT/private/var/db/nord2-vcodec/vendor"
	# a pre-existing, unrelated file must survive
	echo other > "$ROOT/usr/local/bin/not-ours"
	# fake vendor objects in the vendor partition's layout: bin/vpud, lib/lib*.so
	mkdir -p "$ROOT/private/var/db/nord2-vcodec/vendor/bin" "$ROOT/private/var/db/nord2-vcodec/vendor/lib"
	echo "fake vpud" > "$ROOT/private/var/db/nord2-vcodec/vendor/bin/vpud"
	for f in libvpud_vcodec.so libvcodecdrv.so libvcodec_utility.so libvcodec_oal.so; do
		echo "fake $f" > "$ROOT/private/var/db/nord2-vcodec/vendor/lib/$f"
	done
	echo "== install into the fake root =="
	python3 "$OUT/payload/usr/local/libexec/nord2-vcodec-install.py" "$ROOT" --payload "$OUT/payload"
	[ -f "$ROOT/usr/local/bin/vcodecd" ] || [ "$HAVE_DAEMON" -eq 0 ] || { echo "stage host-test: vcodecd missing" >&2; exit 1; }
	[ -f "$ROOT/System/Library/LaunchDaemons/com.nord2.vcodecd.plist" ]
	[ -f "$ROOT/private/var/db/nord2-vcodec/install.json" ]
	[ "$(cat "$ROOT/usr/local/bin/not-ours")" = other ]
	echo "== install again (idempotent) =="
	python3 "$OUT/payload/usr/local/libexec/nord2-vcodec-install.py" "$ROOT" --payload "$OUT/payload" >/dev/null
	echo "== upgrade: a changed file needs --force =="
	printf 'tampered\n' >> "$ROOT/System/Library/LaunchDaemons/com.nord2.vcodecd.plist"
	if python3 "$OUT/payload/usr/local/libexec/nord2-vcodec-install.py" "$ROOT" --payload "$OUT/payload" >/dev/null 2>&1; then
		echo "stage host-test: install replaced a changed file without --force" >&2
		exit 1
	fi
	python3 "$OUT/payload/usr/local/libexec/nord2-vcodec-install.py" "$ROOT" --payload "$OUT/payload" --force >/dev/null
	if grep -q tampered "$ROOT/System/Library/LaunchDaemons/com.nord2.vcodecd.plist"; then
		echo "stage host-test: --force did not restore the payload file" >&2
		exit 1
	fi
	echo "== undo =="
	python3 "$OUT/payload/usr/local/libexec/nord2-vcodec-undo.py" "$ROOT"
	[ ! -f "$ROOT/usr/local/bin/vcodecd" ]
	[ ! -f "$ROOT/System/Library/LaunchDaemons/com.nord2.vcodecd.plist" ]
	[ -f "$ROOT/usr/local/bin/not-ours" ]
	[ -d "$ROOT/private/var/db/nord2-vcodec/vendor" ]
	echo "== re-install and undo --purge-vendor =="
	python3 "$OUT/payload/usr/local/libexec/nord2-vcodec-install.py" "$ROOT" --payload "$OUT/payload" >/dev/null
	python3 "$OUT/payload/usr/local/libexec/nord2-vcodec-undo.py" "$ROOT" --purge-vendor
	[ ! -f "$ROOT/usr/local/bin/vcodecd" ]
	[ ! -d "$ROOT/private/var/db/nord2-vcodec/vendor" ]
	[ -f "$ROOT/usr/local/bin/not-ours" ]
	echo "== full payload then partial payload: receipt keeps the old files =="
	python3 "$HERE/test_install.py"
	echo "STAGE HOST TEST OK"
fi
