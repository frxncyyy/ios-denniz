#!/bin/sh
# Verify the packaged candidate: checksum list, manifest status and no
# accidental device dumps/large binaries.
set -eu
cd "$(dirname "$0")/.."
TARBALL=${1:-out/nord2-gpu-candidate.tar.gz}
test -f "$TARBALL" || { echo "missing $TARBALL" >&2; exit 1; }
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
tar -C "$TMP" -xzf "$TARBALL"
ROOT=$TMP/nord2-gpu-candidate
(cd "$ROOT" && sha256sum -c SHA256SUMS >/dev/null)
python3 - "$ROOT" <<'PY'
import json, sys
root = sys.argv[1]
m = json.load(open(root + '/manifest.json'))
s = m['status']
assert s['device_tested'] is False
assert s['gpu_id_verified'] is False
assert s['default_enabled'] is False
assert m['arch9_compiler']['vertex_idvs'] is True
print('manifest status:', {k: s[k] for k in sorted(s)})
PY
if find "$ROOT" -type f -size +32M | grep -q .; then
  echo "unexpected large file in package" >&2; exit 1
fi
echo "candidate package verified: $TARBALL"
