#!/usr/bin/env python3
"""storage-20261114: end-to-end host+VM rehearsal of the grow transaction and of the grown-volume pipeline.
NEVER touches a phone or the real mirror (both are COPIES under OUT). Run under the global heavy lock:
    flock $HOME/nord2-heavy.lock python3 rehearse_pipeline.py OUT
The "phone" is OUT/dev.img: a sparse file of the real userdata size (224657344 sectors) holding a copy of the mirror (16 GiB root),
fake old-Android garbage past 16 GiB and a fake SWEETLOG tail. The real device shell text (dd/sha256sum/script_apply) runs on it
through fastroot.ShellAdb, exactly like test_fastroot.py.
Steps (= txn_grow.sh, then the next payload transactions):
  A snapshot(16 GiB) -> offline growprep VM -> catalog -> delta commit -> commit_hfs_grow plan+commit -> snapshot(grown, mirror resized)
  B undo (exact) -> snapshot (mirror shrinks) -> re-grow -> snapshot
  C payload final3-undo on the grown sparse mirror: offline VM (107 GiB disk, fsck) -> catalog -> delta commit -> snapshot
  D growfill (allocations past 16 GiB, some freed again) -> catalog -> delta commit (free-space preimage re-base) -> snapshot
  E undo refused after use; independent checks (SWEETLOG untouched except the partition AVH, verified chunks == device).
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import types

S = Path(__file__).resolve().parent
W = S.parents[2]
T = W / 'gpu/tools'
B = W / 'gpu/build'
MAIN = Path('$NORD2_REPO')
sys.path.insert(0, str(T)); sys.path.insert(0, str(B / 'stock-20261114'))
import fastroot as fr                     # noqa: E402
import snapshot_current_root_fast as sf   # noqa: E402
import commit_root_delta_fast as cm       # noqa: E402
import commit_hfs_grow as cg              # noqa: E402

MIB = fr.MIB
PART = 224657344 * 512
OUT = Path(sys.argv[1]).resolve()
OUT.mkdir(exist_ok=True)   # may hold the two fresh copies of a previous attempt (marker 'copied'); everything else must be absent
DEV, MIR = OUT / 'dev.img', OUT / 'mirror.img'
LOG = (OUT / 'rehearsal.log').open('a')


def log(*m):
    line = time.strftime('%H:%M:%S ') + ' '.join(str(x) for x in m)
    print(line, flush=True); LOG.write(line + '\n'); LOG.flush()


def check(cond, what):
    log(('CHECK_OK ' if cond else 'CHECK_FAILED ') + what)
    if not cond:
        raise SystemExit('REHEARSAL_FAILED ' + what)


stat = OUT / 'stat'; stat.write_text(' '.join(['0'] * 17) + '\n')
P = dict(dev=str(DEV), work=str(OUT / 'work'), stat=str(stat), guard='true')
ADB = fr.ShellAdb()


def sha_range(path, off, n):
    with open(path, 'rb') as f:
        f.seek(off); return hashlib.sha256(f.read(n)).hexdigest()


def snapshot(tag):
    t = time.monotonic()
    hdr = open(DEV, 'rb').read(4096)
    bs, cnt = int.from_bytes(hdr[1064:1068], 'big'), int.from_bytes(hdr[1068:1072], 'big')
    r = sf.snapshot(ADB, P, lambda: {'partition_bytes': PART}, MIR, OUT / f'snap-{tag}', bs * cnt, log=lambda *m: log(' ', *m))
    log(f'SNAPSHOT {tag}: {round(time.monotonic() - t)} s', json.dumps({k: (len(v) if k == 'unverified_chunks' else v) for k, v in r.items()
                                                                       if k not in ('changed_big_chunks', 'epoch')}))
    check(MIR.stat().st_size == bs * cnt, f'{tag}: mirror size == volume size {bs * cnt}')
    # independent proof: every verified chunk of the mirror equals the device (fresh hashes of both, not the tool's)
    unv = set(r.get('unverified_chunks', []))
    ver = [i for i in range(bs * cnt // fr.BIG) if i not in unv]
    a = fr.hash_file_chunks(MIR, fr.BIG, ver); d = fr.hash_file_chunks(DEV, fr.BIG, ver)
    check(all(a[i] == d[i] for i in ver), f'{tag}: {len(ver)} verified chunks of mirror == device (independent re-hash)')
    return OUT / f'snap-{tag}/verified.json', r


def offline(tag, payload, guest, snap_receipt):
    out = OUT / f'offline-{tag}'
    t = time.monotonic()
    rc = subprocess.run([sys.executable, str(B / 'apps-20261104/generic_update_offline.py'), '--snapshot-receipt', str(snap_receipt),
                         '--payload', str(payload), '--guest', str(guest), '--output', str(out),
                         '--initramfs', str(MAIN / 'research/nord2/rootfs-vm/initramfs')],
                        stdout=open(OUT / f'offline-{tag}.log', 'w'), stderr=subprocess.STDOUT, timeout=1800).returncode
    vm = (out / 'vm.log').read_text(errors='replace') if (out / 'vm.log').exists() else ''
    for line in vm.splitlines():
        if any(k in line for k in ('GROWPREP_', 'FILL_', 'FINAL3_', 'HAPTIC1_', 'CAMFLASHON_', 'HAPTICPREF_', 'appears to be OK', 'repaired',
                                   'METAL_JOURNAL_APPLIED', 'METAL_UPDATE_FSCK_RESULT', '/dev/vda ', 'Out of memory', 'oom')):
            log('  vm:', line[:220])
    check(rc == 0, f'offline {tag}: generic_update_offline.py exit 0 ({round(time.monotonic() - t)} s)')
    v = json.loads((out / 'verified.json').read_text())
    log(f'  offline {tag}: changed_chunks {v["changed_chunks"]} overlay_allocated {v["overlay_allocated_chunks"]} bytes {v["bytes"]}')
    return out, v


def catalog(tag, snap_receipt, upd, owned):
    rc = subprocess.run([sys.executable, str(T / 'check_update_catalog_fast.py'), '--hfsx-read', str(MAIN / 'research/nord2/display-capabilities/hfsx-read.py'),
                         '--snapshot-receipt', str(snap_receipt), '--update', str(upd), '--owned-json', str(owned),
                         '--output', str(OUT / f'catalog-{tag}.json')], capture_output=True, text=True, timeout=600)
    log(f'  catalog {tag}:', rc.stdout.strip()[-400:], rc.stderr.strip()[-300:])
    check(rc.returncode == 0, f'catalog {tag}: no change outside the owned paths')


def commit(tag, snap_receipt, upd, v):
    a = types.SimpleNamespace(snapshot_receipt=snap_receipt, update=upd, output=OUT / f'install-{tag}', expected_addon_sha256=v['addon_sha256'],
                              commit=True, resume=False, batch_mib=64, jobs=4, advance_mirror=True)
    cm.run(ADB, P, lambda: {}, a, log=lambda *m: log(' ', *m))
    r = json.loads((OUT / f'install-{tag}/verified.json').read_text())
    log(f'  commit {tag}:', json.dumps({k: r[k] for k in ('changed_bytes', 'pre_install_device_check', 'free_space_preimages_rebased')}))
    # every delta chunk on the device is now the 'after'
    ok = all(sha_range(DEV, e['chunk'] * MIB, MIB) == e['after'] for e in v['delta'])
    check(ok, f'commit {tag}: all {len(v["delta"])} delta chunks on the device == after')
    return r


def grow(tag, mode):
    a = types.SimpleNamespace(output=OUT / f'grow-{tag}', reserve_blocks=16384, commit=(mode == 'commit'), undo=None)
    if mode == 'undo':
        a = types.SimpleNamespace(output=OUT / f'grow-{tag}-undo', reserve_blocks=16384, commit=False, undo=OUT / f'grow-{tag}')
    cg.run(ADB, P, PART, lambda: {}, a, log=lambda m: log(' ', m))


def touched_chunks(tag):
    return [c['chunk'] for c in json.loads((OUT / f'grow-{tag}/plan.json').read_text())['chunks']]


t0 = time.monotonic()
# ---------------------------------------------------------------- setup: copies only
src = B / 'mirror/rootfs-mirror.img'
st0 = src.stat()
if (OUT / 'copied').exists() and json.loads((OUT / 'copied').read_text()) == [st0.st_mtime_ns, st0.st_size] \
        and DEV.stat().st_size == MIR.stat().st_size == st0.st_size:
    log('reusing the fresh copies of the mirror (same source mtime)')
else:
    log('copying the mirror (read-only source) to dev.img and mirror.img')
    subprocess.run(['cp', '--sparse=always', str(src), str(DEV)], check=True)
    subprocess.run(['cp', '--sparse=always', str(src), str(MIR)], check=True)
    (OUT / 'copied').write_text(json.dumps([st0.st_mtime_ns, st0.st_size]))
DEV.chmod(0o600); MIR.chmod(0o600)
os.truncate(DEV, PART)
with open(DEV, 'r+b') as f:
    rnd = lambda n: hashlib.shake_256(os.urandom(16)).digest(n)
    # old Android data past the old end: 1 MiB at every 16 MiB of the first 4 GiB past 16 GiB (also inside chunk 1024, after the
    # bitmap extension), plus a few far away, plus a fake SWEETLOG tail (last 4 MiB + 4 KiB of the partition)
    for k in range(256):
        f.seek((16 << 30) + k * 16 * MIB + 8 * MIB); f.write(rnd(MIB))
    for off in (50 << 30, 80 << 30, 106 << 30):
        f.seek(off); f.write(rnd(4 * MIB))
    f.seek(PART - 4 * MIB - 4096); f.write(rnd(4 * MIB + 4096))
    # "iOS was running": clear the cleanly-unmounted bit (primary VH) as a booted phone leaves it
    f.seek(1024); vh = bytearray(f.read(512)); vh[6] &= ~0x01; f.seek(1024); f.write(vh)
sweet_before = sha_range(DEV, PART - 4 * MIB - 4096, 4 * MIB + 4096 - 4096)   # all but the last block (partition AVH lives there)
check(src.stat().st_mtime_ns == st0.st_mtime_ns, 'real mirror untouched by the copy')

# ---------------------------------------------------------------- A: the grow transaction (txn_grow.sh)
s1, r1 = snapshot('A1-16g')
check(r1['bytes'] == 16 << 30 and 'unverified_chunks' not in r1 and r1['changed_big_chunks'] == [0], 'A1: 16 GiB path, only chunk 0 (VH bit) refreshed')
upd, v = offline('A-growprep', S / 'payload-growprep', S / 'guest-growprep.sh', s1)
vm = (upd / 'vm.log').read_text(errors='replace')
check('GROWPREP_READY' in vm, 'A: guest reports GROWPREP_READY')
catalog('A-growprep', s1, upd, S / 'owned-growprep.json')
commit('A-growprep', s1, upd, v)
pre_grow = {}
grow('A', 'plan')
ch = touched_chunks('A')
pre_grow = {i: sha_range(DEV, i * MIB, MIB) for i in ch}
log('  grow touches 1 MiB chunks', ch)
check(all(i * MIB < 16 << 30 or i * MIB >= PART - 5 * MIB or (16 << 30) <= i * MIB < 115024560128 for i in ch), 'A: chunk set sane')
grow('A', 'commit')
s2, r2 = snapshot('A2-grown')
check(r2['bytes'] == 28049408 * 4096 and r2['volume_grown'] and r2['mirror_resized'] == [16 << 30, 28049408 * 4096], 'A2: grown volume detected, sparse mirror resized')
check(1024 not in r2['unverified_chunks'] and 6847 not in r2['unverified_chunks'], 'A2: bitmap-extension chunk and alternate-VH chunk verified')
check(all(c in r2['unverified_chunks'] for c in (1025, 1034, 50 * 64, 80 * 64, 106 * 64)), 'A2: garbage-only free chunks not compared')
check(r2['downloaded_bytes'] <= 16 * MIB, f'A2: downloaded only {r2["downloaded_bytes"]} bytes (no free-space garbage)')
du = os.stat(MIR).st_blocks * 512
log(f'  mirror apparent {MIR.stat().st_size} allocated {du}')
check(du < 18 << 30, 'A2: mirror stays sparse (< 18 GiB allocated)')

# ---------------------------------------------------------------- B: exact undo while iOS has not booted, then re-grow
grow('A', 'undo')
check(all(sha_range(DEV, i * MIB, MIB) == pre_grow[i] for i in ch), 'B: undo restored all touched chunks byte-exact')
s3, r3 = snapshot('B1-undone')
check(r3['bytes'] == 16 << 30 and MIR.stat().st_size == 16 << 30 and 'unverified_chunks' not in r3, 'B1: snapshot after undo: mirror shrunk back to 16 GiB, full verification')
grow('B', 'plan'); grow('B', 'commit')
s4, r4 = snapshot('B2-regrown')

# ---------------------------------------------------------------- C: a normal payload transaction on the grown volume
upd, v = offline('C-final3undo', B / 'final3-20261114/payload-final3-undo', B / 'final3-20261114/guest-final3-undo.sh', s4)
check(v['bytes'] == 28049408 * 4096, 'C: offline VM ran on the full grown volume')
vm = (upd / 'vm.log').read_text(errors='replace')
check(vm.count('appears to be OK') >= 2 and 'FINAL3_OK' in vm, 'C: guest FINAL3_OK and fsck_hfs OK (twice) on the grown volume')
catalog('C-final3undo', s4, upd, B / 'final3-20261114/owned-final3-undo.json')
commit('C-final3undo', s4, upd, v)
s5, r5 = snapshot('C-after')
check(r5['changed_big_chunks'] == [], 'C: next snapshot finds the advanced mirror identical to the device')

# ---------------------------------------------------------------- D: allocations past the old end, some freed again
upd, v = offline('D-growfill', S / 'payload-growfill', S / 'guest-growfill.sh', s5)
beyond = [e for e in v['delta'] if e['chunk'] * MIB >= 16 << 30]
log(f'  D: {len(beyond)} delta chunks past 16 GiB')
check(len(beyond) > 100, 'D: the guest really allocated past the old end')
catalog('D-growfill', s5, upd, S / 'owned-growfill.json')
rd = commit('D-growfill', s5, upd, v)
check(rd['free_space_preimages_rebased'] > 0, 'D: free-space preimages re-based (device garbage under formerly free chunks)')
s6, r6 = snapshot('D-after')
alloc_now = 6848 - len(r6['unverified_chunks'])
check(alloc_now > r5['verified_chunks'], f'D: newly allocated chunks are now verified ({r5["verified_chunks"]} -> {alloc_now})')
upd, v = offline('D-check', S / 'payload-growprep', S / 'guest-growprep.sh', s6)
check('GROWPREP_ALREADY_GROWN' in (upd / 'vm.log').read_text(errors='replace'), 'D: fsck clean + already-grown state after the fill')

# ---------------------------------------------------------------- E: undo after use must refuse; SWEETLOG safety
try:
    grow('B', 'undo'); refused = False
except Exception as e:
    refused = True; log('  undo refused as expected:', str(e)[:200])
check(refused, 'E: undo refused after iOS-like use of the new space')
check(sha_range(DEV, PART - 4 * MIB - 4096, 4 * MIB) == sweet_before, 'E: SWEETLOG tail untouched (except the partition-AVH block)')
check(src.stat().st_mtime_ns == st0.st_mtime_ns and src.stat().st_size == 16 << 30, 'E: real mirror untouched')
log(f'REHEARSAL_DONE {round(time.monotonic() - t0)} s')
