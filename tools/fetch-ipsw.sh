#!/usr/bin/env bash
# Download the iPhone X iOS 16.7.16 (20H392) restore IPSW from Apple and verify it.
#
# The port uses the unmodified iPhone10,3/iPhone10,6 userland. This repository never ships Apple files:
# fetch the IPSW yourself. The download URL is resolved through the public ipsw.me API (which points at
# Apple's own update CDN); the file is accepted only if its sha256 equals the one of the IPSW used for
# the port (recorded on the author's host, 2026-11-17).
#
# Usage: tools/fetch-ipsw.sh [OUTDIR]        (default OUTDIR: ./ipsw)
#        IPSW_URL=<url> tools/fetch-ipsw.sh   (skip the API lookup, e.g. a URL copied from Apple/ipsw.me)
set -euo pipefail
OUT=${1:-ipsw}
NAME='iPhone10,3,iPhone10,6_16.7.16_20H392_Restore.ipsw'
SHA256=ece1194f7e233e9109c5790a8d23a4564a10a254e15ec5b09978a343265568fb
SIZE=6171035648
mkdir -p "$OUT"
F="$OUT/$NAME"
if [ -z "${IPSW_URL:-}" ]; then
    command -v jq >/dev/null || { echo "need jq (or set IPSW_URL)"; exit 2; }
    IPSW_URL=$(curl -fsSL 'https://api.ipsw.me/v4/device/iPhone10,3?type=ipsw' |
               jq -r '.firmwares[] | select(.buildid=="20H392") | .url' | head -1)
    [ -n "$IPSW_URL" ] && [ "$IPSW_URL" != null ] || { echo "20H392 not found through api.ipsw.me; set IPSW_URL"; exit 2; }
fi
echo "URL: $IPSW_URL"
case "$IPSW_URL" in
    https://updates.cdn-apple.com/*|https://secure-appldnld.apple.com/*|https://appldnld.apple.com/*) ;;
    *) echo "refusing a non-Apple URL (override by downloading it yourself and placing it at $F)"; exit 2 ;;
esac
[ -f "$F" ] || curl -fL --retry 5 -C - -o "$F.part" "$IPSW_URL" && { [ -f "$F" ] || mv "$F.part" "$F"; }
[ "$(stat -c %s "$F")" = "$SIZE" ] || { echo "size mismatch: $(stat -c %s "$F") != $SIZE"; exit 1; }
echo "$SHA256  $F" | sha256sum -c -
echo "IPSW_OK $F"
echo "Next: extract OS/SystemOS/AppOS disk images (BuildManifest.plist names them) and run tools/mkiosroot.sh."
