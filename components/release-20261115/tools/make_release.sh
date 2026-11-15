#!/bin/bash
# make_release.sh OUT_DIR   (OWNER side; host only; never touches a phone)
# Builds a clean release directory for another person's Nord 2:
#   1. kernel: strip the chosen kernel, turn the 7 per-device constants into placeholders (n2dev/n2_kernel_pins.py template),
#      scrub owner build paths; ADT: remove the owner's Wi-Fi calibration + MAC (n2dev/mk_device_adt.py --make-template);
#   2. copy ONLY the allowlisted generic files (release-files.txt; every entry is checked against the deny rules);
#   3. release-manifest.json (boot args, RAM window, kernel constants the recipient's phone must match, supported firmware,
#      component hashes), MANIFEST.sha256, README (it/en), licences;
#   4. sanitize_release.py with the owner's private identifier list: any finding -> FAIL, OUT_DIR renamed OUT_DIR.REJECTED;
#   5. tests/run_host_tests.sh from inside the release (synthetic fixtures only).
# All owner paths come from a private env file (default: ../private/release.env, mode 600), so this script holds none.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); RD=$(dirname "$H")
ENV=${RELEASE_ENV:-$RD/private/release.env}
[ -f "$ENV" ] || { echo "missing $ENV (owner-private config: see RELEASE_PLAN.md section 6)"; exit 2; }
. "$ENV"
: "${OWNER_KERNEL:?}" "${OWNER_STAGE_DEV:?}" "${OWNER_ADT:?}" "${OWNER_RECEIPT:?}" "${OWNER_GPT_CRC:?}" "${OWNER_IDS:?}" "${STRIP:?}"
: "${OWNER_SCRUB:?}" "${FILES_LIST:=$H/release-files.txt}" "${SUPPORTED_BOOT_SHA256:?}" "${SUPPORTED_RECOVERY_SHA256:?}"
OUT=${1:?OUT_DIR}; [ ! -e "$OUT" ] || { echo "$OUT exists"; exit 2; }
case "$(readlink -f "$OUT")" in "$(readlink -f "$RD/private")"*) echo "refusing to build inside private/"; exit 2;; esac
mkdir -p "$OUT"/{kernel,host-tools/tools,tools/n2dev,tests,payloads,docs}
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
echo "== 1. kernel template + ADT template"
"$STRIP" "$OWNER_KERNEL" -o "$T/kernel.strip"
python3 -I "$H/n2dev/n2_kernel_pins.py" template "$T/kernel.strip" --stage-dev "$OWNER_STAGE_DEV" --out "$OUT/kernel/kernel.tpl" \
  --map "$OUT/kernel/pins-map.json" --owner-crc "$OWNER_GPT_CRC" $(for s in $OWNER_SCRUB; do echo --scrub "$s"; done)
python3 -I "$H/n2dev/mk_device_adt.py" --make-template "$OWNER_ADT" "$OUT/kernel/template.adt"
echo "== 2. allowlisted files"
n=0
while IFS=$'\t' read -r dest src; do
  [ -z "${dest// }" ] || [ "${dest#\#}" != "$dest" ] && continue
  src=$(eval echo "$src")
  case "$src" in *"/private/"*|*"/secrets"*|*"/mirror/"*|*rootfs-mirror*|*nvdisk*|*"/runs/"*|*IOS-BACKUP*|*cc-txn-*|*cc-fetch-*)
    echo "DENIED by rule: $dest"; exit 3;; esac
  [ -e "$src" ] || { echo "missing source for $dest"; exit 3; }
  mkdir -p "$OUT/$(dirname "$dest")"
  if [ -d "$src" ]; then cp -a --no-preserve=ownership "$src" "$OUT/$dest"; else cp --no-preserve=ownership "$src" "$OUT/$dest"; fi
  n=$((n+1))
done < "$FILES_LIST"
find "$OUT" -name __pycache__ -type d -prune -exec rm -rf {} +
cp "$H"/n2dev/*.py "$H"/n2dev/*.sh "$OUT/tools/n2dev/"; cp "$H/sanitize_release.py" "$H/known-upstream-pem.json" "$OUT/tools/"
cp "$RD"/tests/*.py "$RD"/tests/*.sh "$OUT/tests/"
for f in README-RECIPIENT.md LICENSES-AND-CREDITS.md; do cp "$RD/$f" "$OUT/"; done
# owner adb path defaults in the host tools -> plain 'adb' (the recipient passes --adb anyway)
grep -rlI "${OWNER_ADB_PATH:-/nonexistent}" "$OUT/host-tools" 2>/dev/null | while read -r f; do sed -i "s|$OWNER_ADB_PATH|adb|g" "$f"; done || true
echo "   $n allowlisted entries copied"
echo "== 3. manifest"
python3 -I - "$OWNER_RECEIPT" "$OWNER_STAGE_DEV" "$OUT" "$SUPPORTED_BOOT_SHA256" "$SUPPORTED_RECOVERY_SHA256" <<'EOF'
import hashlib, json, os, re, sys
rec = json.load(open(sys.argv[1])); src = open(os.path.join(sys.argv[2], 'sweet_ufs.c')).read(); out = sys.argv[3]
d = lambda n: int(re.search(r'#define\s+%s\s+(\d+)ull' % n, src).group(1))
kc = {'lun_blocks': d('N2_VENDOR_LUN_BLOCKS'), 'gpt_last_usable': d('N2_VENDOR_LUN_BLOCKS') - 9, 'gpt_first_usable': 34,
      'gpt_entries': 128, 'gpt_entry_lba': 2, 'super_index': 54, 'super_first': d('N2_SUPER_FIRST'), 'super_bytes': d('N2_SUPER_BYTES'),
      'vendor_offset': d('N2_VENDOR_OFFSET'), 'vendor_bytes': d('N2_VENDOR_BYTES'), 'lp_backup_offset': 143360, 'lp_metadata_max': 65536}
assert 'last != %dull' % kc['gpt_last_usable'] in src and '54u * 128u' in src and '143360' in src, 'kernel source changed: update make_release.sh'
files = {}
for dp, _, fs in os.walk(out):
    for f in fs:
        p = os.path.join(dp, f); files[os.path.relpath(p, out)] = hashlib.sha256(open(p, 'rb').read()).hexdigest()
m = {'format': 'nord2-release-1', 'commandline': rec['commandline'], 'phys_base': rec['phys_base'], 'mem_size': rec['mem_size'],
     'boot_layout': rec.get('boot_compress', 'stock'), 'kernel_constants': kc,
     'supported_firmware': {'boot': sys.argv[4], 'recovery': sys.argv[5]},
     'kernel_template_sha256': files['kernel/kernel.tpl'], 'adt_template_sha256': files['kernel/template.adt'],
     'device_tested_on_second_unit': False, 'files': len(files)}
json.dump(m, open(os.path.join(out, 'release-manifest.json'), 'w'), indent=1)
print('   manifest: %d files, kernel constants %d' % (len(files), len(kc)))
EOF
( cd "$OUT" && find . -type f ! -name MANIFEST.sha256 -print0 | sort -z | xargs -0 sha256sum > MANIFEST.sha256 )
echo "== 4. sanitizer (owner identifiers + structural)"
if ! python3 -I "$H/sanitize_release.py" "$OUT" --ids "$OWNER_IDS" --report "$RD/private/scan/release-$(basename "$OUT").json"; then
  mv "$OUT" "$OUT.REJECTED"; echo "RELEASE_REJECTED: see $RD/private/scan/release-$(basename "$OUT").json (paths/classes only)"; exit 1
fi
echo "== 5. host tests from inside the release (synthetic)"
bash "$OUT/tests/run_host_tests.sh" "$T/tests" | tail -3
echo "RELEASE_OK $OUT ($(du -sh "$OUT" | cut -f1)); manifest sha256 $(sha256sum "$OUT/release-manifest.json" | cut -c1-16)"
