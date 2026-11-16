#!/usr/bin/env python3
"""Host model of what OUR kernel checks on a Mach-O (xnu-8792 cs_validate_csblob + page validation; our sweet_amfi.c accepts any
signature that passes these): for every arm64 slice: superblob/CD structure, best CD = highest hash rank (SHA-384 > SHA-256 > SHA-256/20 >
SHA-1), no duplicate rank, every code page hash == CD hash, entitlements blob hash == special slot -5 and DER slot -7 when present.
Prints one line per slice; exit 1 on any failure.  usage: csverify.py [--quiet] FILE... (directories are walked)"""
import hashlib, os, struct, sys

RANK = {1: 1, 3: 2, 2: 3, 4: 4}
HF = {1: hashlib.sha1, 2: hashlib.sha256, 3: hashlib.sha256, 4: hashlib.sha384}

def slices(b):
    m = b[:4]
    if m in (b'\xca\xfe\xba\xbe', b'\xca\xfe\xba\xbf'):
        n = struct.unpack('>I', b[4:8])[0]; out = []
        for i in range(n):
            if m == b'\xca\xfe\xba\xbe':
                ct, st, off, size, _ = struct.unpack('>iiIII', b[8 + 20 * i:28 + 20 * i])
            else:
                ct, st, off, size, _, _ = struct.unpack('>iiQQII', b[8 + 32 * i:40 + 32 * i])
            out.append((ct & 0xffffffff, st & 0xffffffff, b[off:off + size]))
        return out
    if m == b'\xcf\xfa\xed\xfe':
        ct, st = struct.unpack('<ii', b[4:12]); return [(ct & 0xffffffff, st & 0xffffffff, b)]
    return None

def check(mb):
    ncmds = struct.unpack('<I', mb[16:20])[0]; p = 32; cs = None
    for _ in range(ncmds):
        cmd, size = struct.unpack('<II', mb[p:p + 8])
        if cmd == 0x1d: cs = struct.unpack('<II', mb[p + 8:p + 16])
        p += size
    if not cs: return 'UNSIGNED'
    off, size = cs; blob = mb[off:off + size]
    magic, length, count = struct.unpack('>III', blob[:12])
    if magic != 0xfade0cc0 or length > size: return 'bad superblob'
    best = None; ents = der = None; ranks = set()
    for i in range(count):
        typ, o = struct.unpack('>II', blob[12 + 8 * i:20 + 8 * i])
        if o > length: return 'index out of blob'
        m, l = struct.unpack('>II', blob[o:o + 8])
        if o + l > length: return 'sub-blob out of blob'
        if typ == 0 or 0x1000 <= typ < 0x1005:
            if m != 0xfade0c02: return 'CD magic'
            ht = blob[o + 37]; r = RANK.get(ht)
            if r is None: return 'unknown hash type %d' % ht
            if r in ranks: return 'duplicate hash rank (kernel rejects)'
            ranks.add(r)
            if best is None or r > best[0]: best = (r, o, l)
        elif typ == 5:
            if m != 0xfade7171: return 'ents magic'
            ents = blob[o:o + l]
        elif typ == 7:
            if m != 0xfade7172: return 'der ents magic'
            der = blob[o:o + l]
    if not best: return 'no CD'
    _, o, l = best; cd = blob[o:o + l]
    ver, flags, hoff, idoff, nspecial, ncode, climit, hsize, htype, plat, pgshift = struct.unpack('>IIIIIIIBBBB', cd[8:40])
    hf = HF[htype]; page = 1 << pgshift if pgshift else climit
    if climit > off: return 'codeLimit past signature offset'
    for n in range(ncode):
        a = n * page; z = min(a + page, climit)
        if hf(mb[a:z]).digest()[:hsize] != cd[hoff + n * hsize:hoff + (n + 1) * hsize]:
            return 'PAGE HASH MISMATCH page %d' % n
    def special(k):
        if k > nspecial: return None
        return cd[hoff - k * hsize:hoff - (k - 1) * hsize]
    for k, b in ((5, ents), (7, der)):
        s = special(k)
        if b is not None:
            if s is None or hf(b).digest()[:hsize] != s: return 'entitlements slot -%d mismatch (kernel refuses the blob)' % k
        elif s is not None and s != b'\0' * hsize: return 'slot -%d set but blob missing' % k
    ident = cd[idoff:].split(b'\0')[0].decode(errors='replace')
    return 'OK ht%d pages%d flags%#x ents%s ident=%s' % (htype, ncode, flags, 'Y' if ents else 'N', ident)

def files(args):
    for a in args:
        if os.path.isdir(a):
            for b, ds, fs in os.walk(a):
                for f in fs:
                    p = os.path.join(b, f)
                    if not os.path.islink(p): yield p
        else: yield a

def main():
    q = '--quiet' in sys.argv; bad = 0; n = 0
    for f in files([a for a in sys.argv[1:] if a != '--quiet']):
        try: b = open(f, 'rb').read(64 * 1024 * 1024)
        except OSError: continue
        sl = slices(b)
        if not sl: continue
        for ct, st, mb in sl:
            if ct != 0x0100000c: continue
            if mb[:4] != b'\xcf\xfa\xed\xfe':
                continue
            if struct.unpack('<I', mb[12:16])[0] not in (2, 6, 8): continue   # execute, dylib, bundle
            r = check(mb); n += 1
            if not r.startswith('OK'): bad += 1
            if not q or not r.startswith('OK'): print(f, 'sub%d' % (st & 0xff), r)
    print('CSVERIFY slices=%d bad=%d' % (n, bad))
    sys.exit(1 if bad else 0)

if __name__ == "__main__":
    main()
