#!/usr/bin/env python3
"""host test of block-sbshift1.py on a scratch copy of the stock subcache: install, re-install no-op, full code-directory verify,
disassembly of the patched sites, undo, re-undo no-op, byte identity with stock; refusals on a foreign/mixed file.
usage: test_sbshift1.py STOCK_SUBCACHE SCRATCH_DIR"""
import sys, os, shutil, hashlib, struct, subprocess, json
H = os.path.dirname(os.path.abspath(__file__)); BLOCK = open(os.path.join(H, '..', 'block-sbshift1.py')).read()
stock, scratch = sys.argv[1], sys.argv[2]
R = os.path.join(scratch, 'root'); shutil.rmtree(R, ignore_errors=True)
dst = R + '/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64.03'; os.makedirs(os.path.dirname(dst)); shutil.copyfile(stock, dst)
def blk(undo):
    g = {'R': R, 'UNDO': undo, 'NO_RUN': True}; exec(BLOCK, g); return g['run'](undo)
def sha(p): return hashlib.sha256(open(p, 'rb').read()).hexdigest()
def verify_all(p):
    g = {'R': R, 'NO_RUN': True}; exec(BLOCK, g)
    with open(p, 'rb') as f:
        cd = g['cd_info'](f); bad = [pg for pg in range(cd['ncode']) if g['page_hash'](f, cd, pg) != g['stored_hash'](f, cd, pg)]
    return cd['ncode'], bad
fails = []
def ok(c, m):
    print(('PASS ' if c else 'FAIL ') + m); c or fails.append(m)
s0 = sha(dst)
ok(blk(False) == 'install', 'install')
n, bad = verify_all(dst); ok(not bad, 'all %d page hashes match after install (bad %s)' % (n, bad[:5]))
ok(blk(False) == 'noop', 'second install is a no-op')
rec = json.load(open(R + '/private/var/db/nord2-stock/sbshift1.json')); ok(rec['sha_before'] == s0, 'receipt sha_before = stock')
# disassemble the patched bytes
def dis(off, n):
    b = open(dst, 'rb').read()[off:off + n]; t = os.path.join(scratch, 'x.bin'); open(t, 'wb').write(b)
    return subprocess.run(['llvm-objdump', '-D', '-b', 'binary', '--triple=arm64', t], capture_output=True, text=True).stdout if False else \
        subprocess.run(['llvm-mc', '--disassemble', '-triple=arm64'], input=' '.join('0x%02x' % x for x in b), capture_output=True, text=True).stdout
a = dis(0x24a2754, 12); b = dis(0x2d2e7b0, 4); print(a, b)
ok('#0x4042, lsl #48' in a.replace('\t', ' ') or '4630263366890291200' in a, 'site A = movz x8,#0x4042,lsl#48'); ok('fmov\td0, x8' in a and 'ret' in a, 'site A fmov d0,x8; ret')
ok(struct.unpack('>d', bytes.fromhex('4042000000000000'))[0] == 36.0, 'site A constant = 36.0'); ok('#-25.0' in b, 'site B = fmov d0,#-25.0')
ok(blk(True) == 'undo', 'undo'); ok(sha(dst) == s0, 'undo restores the stock file byte for byte')
ok(not os.path.exists(R + '/private/var/db/nord2-stock/sbshift1.json'), 'undo removes the receipt'); ok(blk(True) == 'noop', 'second undo is a no-op')
# refusals: one site changed by someone else
with open(dst, 'r+b') as f: f.seek(0x2d2e7b0); f.write(b'\0\0\0\0')
ok(blk(False) == 'refused' and sha(dst) != s0 and not os.path.exists(R + '/private/var/db/nord2-stock/sbshift1.json'), 'foreign bytes at a site -> refused, nothing written')
shutil.copyfile(stock, dst)
with open(dst, 'r+b') as f: f.seek(0x100000); f.write(b'X')     # stock sites, other content differs: page hash ok elsewhere? full sha refuses
ok(blk(False) == 'refused', 'non-stock subcache (full sha) -> refused'); os.remove(dst)
print('RESULT', 'FAIL' if fails else 'ALL PASS', fails)
shutil.rmtree(R); sys.exit(1 if fails else 0)
