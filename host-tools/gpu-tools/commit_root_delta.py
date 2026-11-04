#!/usr/bin/env python3
"""Verify an offline Metal install and commit only its changed 1 MiB ranges.

Explicit --commit requires the already obtained human addon authorization.
No reboot, boot flash, partition resizing or unrelated partition access.
On interruption stay in RAM rescue; --resume validates every device chunk
against its exact before/after digest before continuing a partial transaction.
By default the final whole-device SHA-256 is also checked. An explicit
--skip-final-full-hash retains preimage and changed-range checks and records
that no final whole-device verification was performed.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess

CHUNK = 1 << 20
BY_NAME = '/dev/block/by-name/userdata'
DEVICE = '/dev/block/sdc57'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def classify(actual, before, after):
    if actual == after:
        return 'done'
    if actual == before:
        return 'pending'
    raise RuntimeError('Unknown device preimage; refuse write')


def build_manifest(before, after):
    total_before, total_after = hashlib.sha256(), hashlib.sha256()
    chunks, changed = [], []
    with before.open('rb') as src, after.open('rb') as dst:
        i = 0
        while True:
            a, b = src.read(CHUNK), dst.read(CHUNK)
            if not a and not b:
                break
            if len(a) != CHUNK or len(b) != CHUNK:
                raise ValueError('Unequal or unaligned image sizes')
            total_before.update(a); total_after.update(b)
            chunks.append([sha(a), sha(b)])
            if a != b:
                changed.append(i)
            i += 1
    return {'chunk_bytes': CHUNK, 'bytes': i * CHUNK,
            'before': total_before.hexdigest(), 'after': total_after.hexdigest(),
            'chunks': chunks, 'changed': changed}


def check_receipts(before, after, manifest, expected_addon):
    if not re.fullmatch('[0-9a-f]{64}', expected_addon):
        raise ValueError('invalid expected addon hash')
    if after.get('rehearsal'):
        raise ValueError('PC rehearsal image must never be written to a phone')
    if not before['independent_full_hash_matches'] or not after['offline_verified']:
        raise ValueError('unverified input receipt')
    if after['addon_sha256'] != expected_addon:
        raise ValueError('addon differs from exact approval')
    if manifest['before'] != before['sha256'] or manifest['before'] != after['before']:
        raise ValueError('source snapshot mismatch')
    if manifest['after'] != after['after'] or manifest['bytes'] != 16 << 30:
        raise ValueError('installed clone or geometry mismatch')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--main', type=Path, required=True)
    ap.add_argument('--before', type=Path, required=True)
    ap.add_argument('--after', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--commit', action='store_true')
    ap.add_argument('--resume', action='store_true')
    ap.add_argument('--skip-final-full-hash', action='store_true',
                    help='Skip only the final whole-device hash; retain source/preimage checks and every changed-range readback')
    ap.add_argument('--expected-addon-sha256', default='dece831149851e15bf557d582f575cc4464389a03f13ff0ccbefd6389437ce1c',
                    help='Exact addon hash approved by the user, not inferred from the clone')
    ap.add_argument('--adb', default='$HOME/Android/Sdk/platform-tools/adb')
    a = ap.parse_args()
    before_receipt=json.loads((a.before.parent/'verified.json').read_text())
    after_receipt=json.loads((a.after.parent/'verified.json').read_text())
    m=build_manifest(a.before,a.after)
    check_receipts(before_receipt, after_receipt, m, a.expected_addon_sha256)
    a.output.mkdir(exist_ok=True)
    (a.output/'manifest.json').write_text(json.dumps(m,indent=2)+'\n')
    print(f"Changed {len(m['changed'])} MiB of {m['bytes']//CHUNK} MiB",flush=True)
    if not a.commit:
        return
    marker=a.output/'started.json'
    if marker.exists() and not a.resume:
        raise RuntimeError('Transaction already started; inspect and explicitly resume')
    spec=importlib.util.spec_from_file_location('pf',a.main/'tools/nord2-install-root.py')
    pf=importlib.util.module_from_spec(spec);spec.loader.exec_module(pf)
    devices=subprocess.check_output([a.adb,'devices'],text=True).splitlines()[1:]
    live=[x.split()[0] for x in devices if len(x.split())==2 and x.split()[1] in ('device','recovery')]
    assert len(live)==1
    serial=live[0]
    pf.preflight(a.adb,serial)

    def remote(cmd,data=None,timeout=120):
        return subprocess.check_output([a.adb,'-s',serial,'shell','-T',cmd],input=data,timeout=timeout)

    def whole():
        value=remote(f'set -o pipefail; dd if={BY_NAME} bs={CHUNK} count={m["bytes"]//CHUNK} status=none | sha256sum',timeout=1800).decode().split()[0]
        assert re.fullmatch('[0-9a-f]{64}',value)
        return value

    current=whole()
    full_verified=current==m['after']
    if current == m['after']:
        print('Full device already matches installed clone',flush=True)
    else:
        if current != m['before']:
            if not a.resume or not marker.exists():
                raise RuntimeError('Current full device is not the verified before image')
            old=json.loads(marker.read_text());assert old['before']==m['before'] and old['after']==m['after']
            print('Checking all device chunk preimages for partial resume',flush=True)
            script=('set -e; set -o pipefail; i=0; while [ "$i" -lt 16384 ]; do '
                    f'dd if={BY_NAME} bs={CHUNK} skip=$i count=1 status=none | sha256sum; i=$((i+1)); done')
            hashes=remote(script,timeout=1800).decode().splitlines();assert len(hashes)==len(m['chunks'])
            for line,(b,c) in zip(hashes,m['chunks']): classify(line.split()[0],b,c)
        pf.preflight(a.adb,serial)
        marker.write_text(json.dumps({'before':m['before'],'after':m['after']})+'\n')
        with a.after.open('rb') as f, (a.output/'ranges.jsonl').open('a') as journal:
            for i in m['changed']:
                b,c=m['chunks'][i]
                raw=remote(f'dd if={BY_NAME} bs={CHUNK} skip={i} count=1 status=none')
                assert len(raw)==CHUNK
                if classify(sha(raw),b,c)=='done':
                    continue
                f.seek(i*CHUNK);data=f.read(CHUNK);assert len(data)==CHUNK and sha(data)==c
                command=(f'test -b {BY_NAME} && test "$(readlink -f {BY_NAME})" = {DEVICE} && '
                         'test "$(cat /nord2-rescue)" = "ADB RAM recovery; userdata is not mounted" && '
                         f'dd of={BY_NAME} bs={CHUNK} seek={i} conv=notrunc,fsync status=none')
                remote(command,data)
                raw=remote(f'dd if={BY_NAME} bs={CHUNK} skip={i} count=1 status=none')
                assert len(raw)==CHUNK and sha(raw)==c, 'Written range readback mismatch; retain rescue'
                journal.write(json.dumps({'chunk':i,'before':b,'after':c,'readback_verified':True})+'\n')
                journal.flush();os.fsync(journal.fileno())
                print(f'Committed and read back chunk {i}',flush=True)
        remote('sync')
        if not a.skip_final_full_hash:
            assert whole()==m['after'], 'Final full device hash mismatch; retain rescue'
            full_verified=True
    (a.output/'verified.json').write_text(json.dumps({'before':m['before'],'after':m['after'],
        'changed_bytes':len(m['changed'])*CHUNK,'full_device_readback_verified':full_verified,
        'all_device_preimages_verified':True,'changed_ranges_readback_verified':True,
        'final_full_hash_skipped':a.skip_final_full_hash and not full_verified},indent=2)+'\n')
    if full_verified:
        print('Full device equals installed offline clone; rootfs transaction verified',flush=True)
    else:
        print('All changed ranges read back and verified; final whole-device hash skipped as requested',flush=True)


if __name__=='__main__':main()
