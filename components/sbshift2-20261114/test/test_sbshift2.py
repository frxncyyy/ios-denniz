#!/usr/bin/env python3
"""host test of block-sbshift2.py on a scratch copy of the stock subcache.
usage: test_sbshift2.py STOCK_SUBCACHE SCRATCH_DIR
 stock -> sbshift1 (real sbshift1 block) -> sbshift2-22 -> no-op -> retune 19 -> all page hashes -> sbshift1-undo REFUSED (no write)
 -> sbshift2 undo == sbshift1 byte for byte incl. receipt -> undo no-op -> sbshift1-undo == stock; stock -> sbshift2 -> undo == stock;
 disassembly + constants of every variant; refusals (foreign bytes, wrong sha)."""
import sys, os, shutil, hashlib, struct, subprocess, json
H = os.path.dirname(os.path.abspath(__file__))
B2 = open(os.path.join(H, '..', 'block-sbshift2.py')).read()
B1 = open(os.path.join(H, '..', '..', 'sbshift-20261114', 'block-sbshift1.py')).read()
VJ = json.load(open(os.path.join(H, '..', 'variants.json')))
stock, scratch = sys.argv[1], sys.argv[2]
R = os.path.join(scratch, 'root'); shutil.rmtree(R, ignore_errors=True)
dst = R + '/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64.03'; os.makedirs(os.path.dirname(dst)); shutil.copyfile(stock, dst)
DB = R + '/private/var/db/nord2-stock'
def b2(undo, v=None):
    g = {'R': R, 'NO_RUN': True}; exec(B2, g); return g['run'](undo, v)
def b1(undo):
    g = {'R': R, 'NO_RUN': True}; exec(B1, g); return g['run'](undo)
def sha(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for b in iter(lambda: f.read(1 << 22), b''): h.update(b)
    return h.hexdigest()
def verify_all():
    g = {'R': R, 'NO_RUN': True}; exec(B2, g)
    with open(dst, 'rb') as f:
        cd = g['cd_info'](f); bad = [pg for pg in range(cd['ncode']) if g['page_hash'](f, cd, pg) != g['stored_hash'](f, cd, pg)]
    return cd['ncode'], bad
def dbstate():
    return {n: open(os.path.join(DB, n)).read() for n in sorted(os.listdir(DB))} if os.path.isdir(DB) else {}
fails = []
def ok(c, m):
    print(('PASS ' if c else 'FAIL ') + m); c or fails.append(m)
s0 = sha(dst)
ok(b1(False) == 'install', 'sbshift1 install (phone state)'); s1 = sha(dst); ok(s1 == VJ['sha_sbshift1'], 'sbshift1 sha = recorded 4109f63f9d66')
db1 = dbstate()
ok(b2(False, '22') == 'install', 'sbshift2-22 install from sbshift1'); ok(sha(dst) == VJ['variants']['22']['sha'], 'sha = expected variant 22')
n, bad = verify_all(); ok(not bad, 'all %d page hashes match (bad %s)' % (n, bad[:5]))
ok(not os.path.exists(DB + '/sbshift1.json') and os.path.exists(DB + '/sbshift2.json'), 'sbshift1.json folded into sbshift2.json')
ok(b2(False, '22') == 'noop', 'second install no-op')
s22 = sha(dst); ok(b1(True) == 'refused' and sha(dst) == s22, 'sbshift1-undo refuses while sbshift2 is installed, nothing written')
ok(b1(False) == 'refused' and sha(dst) == s22, 'sbshift1 install refuses on sbshift2 state')
ok(b2(False, '19') == 'install' and sha(dst) == VJ['variants']['19']['sha'], 'retune 22 -> 19')
n, bad = verify_all(); ok(not bad, 'page hashes after retune')
ok(b2(False, '99') == 'refused', 'unknown variant refused')
ok(b2(True) == 'undo', 'undo'); ok(sha(dst) == s1, 'undo == sbshift1 byte for byte'); ok(dbstate() == db1, 'sbshift1.json restored identically, sbshift2.json gone')
ok(b2(True) == 'noop', 'second undo no-op')
ok(b1(True) == 'undo' and sha(dst) == s0, 'then sbshift1-undo -> stock')
ok(b2(False, '24') == 'install' and sha(dst) == VJ['variants']['24']['sha'], 'install from stock (variant 24) = same bytes as via sbshift1')
ok(b2(True) == 'undo' and sha(dst) == s0 and dbstate() in ({},), 'undo from stock base -> stock, no receipts')
# interrupted install: receipt2 written + sbshift1.json still present, bytes patched -> re-run finalizes
b1(False); b2(False, '22'); rec2 = open(DB + '/sbshift2.json').read(); open(DB + '/sbshift1.json', 'w').write(json.loads(rec2)['sbshift1_receipt'])
ok(b2(False, '22') == 'noop' and not os.path.exists(DB + '/sbshift1.json'), 'interrupted install (both receipts) finalized by re-run')
b2(True); b1(True); ok(sha(dst) == s0, 'back to stock')
# disassembly and constants of every variant
g = {'R': R, 'NO_RUN': True}; exec(B2, g)
for k, v in sorted(VJ['variants'].items(), key=lambda x: int(x[0])):
    c1, c2 = bytes.fromhex(v['c'][0]), bytes.fromhex(v['c'][1])
    mc = lambda b: subprocess.run(['llvm-mc', '--disassemble', '-triple=arm64'], input=' '.join('0x%02x' % x for x in b), capture_output=True, text=True).stdout
    t1, t2 = mc(c1), mc(c2)
    sys.path.insert(0, os.path.join(H, '..', '..', 'sbshift-20261114', 'tools'))
    from dsc import read
    from xref import adrp_target
    def target(code, pc):
        i0, i1 = struct.unpack_from('<II', code, 0); pg, rd = adrp_target(i0, pc); rn = (i1 >> 5) & 31
        assert rn == rd and (i1 & 0xffc00000) == 0xfd400000; return pg + ((i1 >> 10) & 0xfff) * 8, i1 & 31
    pa, rp = target(c1, 0x189abab20); ta, rt = target(c2, 0x189abab38)
    pv = struct.unpack('<d', read(pa, 8))[0]; tv = struct.unpack('<d', read(ta, 8))[0]; d = int(k) / 3
    good = rp == 2 and rt == 0 and abs(pv - (d - 5 / 3)) < 1e-9 and abs(tv - (44 / 3 + d)) < 1e-9 and 'fneg\td2, d2' in t1 and 0x189ea52e0 <= pa < 0x189eb1a48 and 0x189ea52e0 <= ta < 0x189eb1a48
    ok(good, 'variant %s: bottom inset -%.4f (d2), top %.4f (d0) -> region moved +%.4f pt = %s px; consts in UIKitCore __TEXT,__const' % (k, pv, tv, tv - 44 / 3, k))
# refusals
with open(dst, 'r+b') as f: f.seek(0x2d2eb38); f.write(b'\0\0\0\0')
ok(b2(False, '22') == 'refused', 'foreign bytes at site C -> refused (page hash mismatch)')
shutil.copyfile(stock, dst)
with open(dst, 'r+b') as f: f.seek(0x100000); f.write(b'X')
ok(b2(False, '22') == 'refused' and not os.path.exists(DB + '/sbshift2.json'), 'non-stock subcache (full sha) -> refused, no receipt')
print('RESULT', 'FAIL' if fails else 'ALL PASS', fails)
shutil.rmtree(R); sys.exit(1 if fails else 0)
