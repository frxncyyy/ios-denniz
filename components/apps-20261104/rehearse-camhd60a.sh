#!/bin/bash
# rehearsal of guest-camhd60a.sh / guest-camhd60a-undo.sh on a fake root: the scripts' own Python, /work/root and /src put elsewhere
set -e
A=$1; PLIST=$2; F=$(mktemp -d); D=$F/System/Library/PrivateFrameworks/CMCapture.framework/D22
mkdir -p "$D"; cp "$PLIST" "$D/AVCaptureSession.plist"
run() { sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$A/$1" | sed '1d;$d' | sed "s|/work/root|$F|g; s|/src/|$A/payload-camhd60a/|g" | python3 -; }
h() { sha256sum "$D/AVCaptureSession.plist" | cut -c1-12; ls "$D" | tr '\n' ' '; echo; }
echo "start:   $(h)"; run guest-camhd60a.sh; echo "install: $(h)"; run guest-camhd60a.sh; echo "again:   $(h)"
run guest-camhd60a-undo.sh; echo "undo:    $(h)"; run guest-camhd60a-undo.sh; echo "again:   $(h)"
echo "--- a file that is neither: must refuse and change nothing"; echo junk > "$D/AVCaptureSession.plist"
run guest-camhd60a.sh 2>&1 | tail -1 || true; echo "after:   $(h)"; rm -rf "$F"
echo REHEARSAL_DONE
