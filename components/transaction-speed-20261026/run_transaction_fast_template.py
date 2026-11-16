"""Template: fast install transaction. Copy next to authorization.json of a run dir and edit RUN/PREV.

Same stage order and approvals as run_transaction.py; stages use the *_fast tools.
Run from the work root (cwd), like the old script. One-time: --seed creates the warm mirror.
"""
from pathlib import Path
import json, subprocess, sys, datetime, argparse
ap = argparse.ArgumentParser(); ap.add_argument('--resume-after-offline', action='store_true')
ap.add_argument('--seed', help='previous verified rootfs image, only for the very first run')
ap.add_argument('--jobs', default='4')
args = ap.parse_args()
p = Path.cwd(); e = Path(__file__).resolve().parent
MIRROR = p / 'gpu/build/mirror/rootfs-mirror.img'          # persistent, warm; keep between transactions
T = p / 'gpu/tools'
a = json.loads((e / 'authorization.json').read_text()); main = Path(a['artifacts']['rescue']['path']).parents[3]
assert a['artifacts']['addon']['sha256'] and a['authorization']
def run(stage, cmd):
    (e / (stage + '-command.json')).write_text(json.dumps(cmd, indent=2) + '\n')
    (e / 'transaction-status.json').write_text(json.dumps({'stage': stage, 'time': datetime.datetime.now(datetime.timezone.utc).isoformat()}) + '\n')
    print(stage, flush=True)
    with (e / (stage + '.log')).open('w') as log: subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, check=True)
MIRROR.parent.mkdir(parents=True, exist_ok=True)
if not args.resume_after_offline:
    run('snapshot', [sys.executable, str(T / 'snapshot_current_root_fast.py'), '--main', str(main), '--mirror', str(MIRROR),
                     '--output', str(e / 'snapshot'), '--jobs', args.jobs] + (['--seed', args.seed] if args.seed else []))
    s = json.loads((e / 'snapshot/verified.json').read_text()); assert s['per_chunk_manifest_matches']
    run('offline-update', [sys.executable, str(T / 'update_metal_offline_fast.py'), '--main', str(main),
        '--snapshot-receipt', str(e / 'snapshot/verified.json'), '--addon', a['artifacts']['addon']['path'],
        '--addon-sha256', a['artifacts']['addon']['sha256'], '--output', str(e / 'offline-update'),
        '--journal-oracle', str(p / 'gpu/build/hardware-20261024/hfs-source/oracle'),
        '--vm-kernel', str(p / 'gpu/build/vm-runtime/vmlinuz-7.2.9-arch1-1')])
# Catalog gate: edit --owned-json if this addon owns other paths (default list = Metal bundle, probe, varying.bc, CA prefs).
run('catalog', [sys.executable, str(T / 'check_update_catalog_fast.py'), '--hfsx-read',
                str(p / '../../../../../research/nord2/display-capabilities/hfsx-read.py'),
                '--snapshot-receipt', str(e / 'snapshot/verified.json'), '--update', str(e / 'offline-update'),
                '--output', str(e / 'catalog-preservation.json')])
c = json.loads((e / 'catalog-preservation.json').read_text()); assert not c['outside_owned_paths']
run('device-install', [sys.executable, str(T / 'commit_root_delta_fast.py'), '--main', str(main),
    '--snapshot-receipt', str(e / 'snapshot/verified.json'), '--update', str(e / 'offline-update'),
    '--output', str(e / 'device-install'), '--expected-addon-sha256', a['artifacts']['addon']['sha256'], '--commit'])
i = json.loads((e / 'device-install/verified.json').read_text())
assert i['all_device_preimages_verified'] and i['changed_ranges_readback_verified']
(e / 'transaction-status.json').write_text(json.dumps({'stage': 'installed-verified-awaiting-manual-fastboot',
    'time': datetime.datetime.now(datetime.timezone.utc).isoformat(), 'expected_after_id': i['after_id'],
    'verification': 'changed-ranges-readback', 'final_full_hash_skipped': True}) + '\n')
print('Installed; changed ranges read back and verified. No reboot issued.', flush=True)
