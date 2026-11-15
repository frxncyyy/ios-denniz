#!/usr/bin/env python3
"""Read-only HFS+/HFSX extractor that replays the journal in memory first.

usage: hfsx.py IMAGE ls   /private/var/db/diagnostics/Persist
       hfsx.py IMAGE get  /private/var/db/diagnostics/Persist OUTDIR [NAME-PREFIX...]   (no prefix: every file)
       hfsx.py IMAGE rget /private/var/db/diagnostics OUTDIR          (the whole tree, data forks only)
       --no-replay: read the image as it is (what 7-Zip sees)

Files with more than 8 extents continue in the extents-overflow B-tree; 7-Zip 26.05's HFS reader
fails on them ("Data Error", 0-byte output): on q10bv that was every fragmented tracev3 file
(Persist 0a 0c 0d 17 19 1a 78 7d 7e 84 86 88 89, Special 04 7b), i.e. the newest, largest ones.

The image is never written: committed-but-not-checkpointed journal transactions
(including valid ones past the header's 'end') are applied as an overlay, exactly
like the kernel's replay_journal() would do at the next mount.
"""
import sys, os, struct

class Img:
    def __init__(self, path):
        self.f = open(path, 'rb')
        self.ov = {}            # 512-byte sector -> bytes(512)
        self.replay_info = 'no journal'

    def raw(self, off, n):
        self.f.seek(off)
        return self.f.read(n)

    def read(self, off, n):
        b = bytearray(self.raw(off, n))
        if self.ov:
            s0, s1 = off // 512, (off + n + 511) // 512
            for s in range(s0, s1):
                d = self.ov.get(s)
                if d is None:
                    continue
                so = s * 512
                lo, hi = max(so, off), min(so + 512, off + n)
                b[lo - off:hi - off] = d[lo - so:hi - so]
        return bytes(b)

    def replay(self):
        vh = self.raw(1024, 512)
        attr, jib = struct.unpack('>I4xI', vh[4:16])
        bs = struct.unpack('>I', vh[0x28:0x2c])[0]
        if not (attr & 0x2000):
            return
        j = self.raw(jib * bs, 52)
        flags = struct.unpack('>I', j[:4])[0]
        joff, jsize = struct.unpack('>QQ', j[36:52])
        if not (flags & 1):
            self.replay_info = 'journal not in fs'
            return
        jh = self.raw(joff, 48)
        E = '<' if struct.unpack('<i', jh[4:8])[0] == 0x12345678 else '>'
        _, _, start, end, size, blhdr_size, _, jhdr_size, _ = struct.unpack(E + 'iiqqqiIiI', jh)

        def cks(b):
            c = 0
            for x in b:
                c = ((c << 8) ^ (c + x)) & 0xffffffff
            return (~c) & 0xffffffff

        def wrap(o):
            return jhdr_size + (o - size) if o >= size else o

        def rj(o, n):
            out = b''
            while n > 0:
                o = wrap(o)
                t = min(n, size - o)
                out += self.raw(joff + o, t)
                o += t
                n -= t
            return out

        if start == size:
            start = jhdr_size
        if end == size:
            end = jhdr_size
        off, lastseq, chunks, blocks, extra = start, 0, 0, 0, 0
        past = False
        while True:
            if off == end:
                past = True
            hdr = rj(off, blhdr_size)
            maxb, numb, _, ck, _ = struct.unpack(E + 'HHiIi', hdr[:16])
            h = bytearray(hdr[:32])
            h[8:12] = b'\0\0\0\0'
            if cks(bytes(h)) != ck or numb <= 0 or numb > maxb:
                break
            seq = struct.unpack(E + 'I', hdr[28:32])[0]
            if lastseq and seq and seq not in (lastseq, lastseq + 1):
                break
            lastseq = seq
            d = wrap(off + blhdr_size)
            for i in range(1, numb):
                bnum, bsz, _ = struct.unpack(E + 'qiI', hdr[16 + 16 * i:32 + 16 * i])
                if bnum != -1:
                    data = rj(d, bsz)
                    for k in range(bsz // 512):
                        self.ov[bnum + k] = data[k * 512:(k + 1) * 512]
                    blocks += 1
                d = wrap(d + bsz)
            chunks += 1
            extra += past
            off = d
            if chunks > 100000:
                break
        self.replay_info = 'journal replayed in memory: %d chunks (%d past header end), %d blocks, %d sectors' % (chunks, extra, blocks, len(self.ov))


class HFS:
    def __init__(self, img):
        self.img = img
        vh = img.read(1024, 512)
        self.sig = vh[:2]
        self.bs = struct.unpack('>I', vh[0x28:0x2c])[0]
        self.ext_fork = self.fork(vh[0xc0:0x110])
        self.cat_fork = self.fork(vh[0x110:0x160])
        self.ext = None
        self.ext = BTree(self, self.ext_fork, 3)
        self.cat = BTree(self, self.cat_fork, 4)

    @staticmethod
    def fork(b):
        size = struct.unpack('>Q', b[:8])[0]
        ex = [struct.unpack('>II', b[16 + 8 * i:24 + 8 * i]) for i in range(8)]
        return size, [e for e in ex if e[1]]

    def fork_extents(self, fork, fileid=None, ftype=0):
        size, ex = fork
        ex = list(ex)
        need = (size + self.bs - 1) // self.bs
        have = sum(c for _, c in ex)
        if have < need and fileid not in (None, 3) and self.ext is not None:
            ex += self.ext.overflow(fileid, ftype, have)
        return size, ex

    def read_fork(self, fork, fileid=None, ftype=0, off=0, n=None):
        size, ex = self.fork_extents(fork, fileid, ftype)
        if n is None:
            n = size - off
        out = bytearray()
        pos = 0
        for sb, cnt in ex:
            elen = cnt * self.bs
            if pos + elen > off and len(out) < n:
                lo = max(off, pos) - pos
                take = min(elen - lo, n - len(out))
                out += self.img.read(sb * self.bs + lo, take)
            pos += elen
        return bytes(out[:n])

    def lookup(self, path):
        cnid = 2
        rec = None
        for comp in [c for c in path.split('/') if c]:
            rec = self.cat.find_child(cnid, comp)
            if rec is None:
                raise FileNotFoundError(path)
            rtype = struct.unpack('>h', rec[:2])[0]
            cnid = struct.unpack('>I', rec[8:12])[0]
        return cnid, rec

    def listdir(self, cnid):
        return self.cat.children(cnid)


class BTree:
    def __init__(self, fs, fork, kind):
        self.fs, self.fork, self.kind = fs, fork, kind
        self.fileid = kind
        hdr = fs.read_fork(fork, kind, 0, 0, 512)
        (self.depth, self.root, self.leafrecs, self.first, self.last, self.nodesize,
         _, self.total, _) = struct.unpack('>HIIIIHHII', hdr[14:44])
        self.cache = {}

    def node(self, i):
        n = self.cache.get(i)
        if n is None:
            n = self.fs.read_fork(self.fork, self.fileid, 0, i * self.nodesize, self.nodesize)
            self.cache[i] = n
        flink, blink, kind, height, nrec = struct.unpack('>IIbBH', n[:12])
        offs = [struct.unpack('>H', n[self.nodesize - 2 * (k + 1):self.nodesize - 2 * k])[0] for k in range(nrec + 1)]
        recs = [n[offs[k]:offs[k + 1]] for k in range(nrec)]
        return flink, kind, height, recs

    @staticmethod
    def catkey(r):
        klen, parent, nlen = struct.unpack('>HIH', r[:8])
        name = r[8:8 + 2 * nlen].decode('utf-16-be', 'replace')
        return parent, name, r[2 + klen:]

    @staticmethod
    def cmpkey(a, b):
        # HFSX binary compare: parent id, then UTF-16 code units
        if a[0] != b[0]:
            return -1 if a[0] < b[0] else 1
        ua, ub = a[1].encode('utf-16-be'), b[1].encode('utf-16-be')
        return (ua > ub) - (ua < ub)

    def find_child(self, parent, name):
        want = (parent, name)
        i = self.root
        while True:
            _, kind, _, recs = self.node(i)
            if kind == -1:
                for r in recs:
                    p, nm, data = self.catkey(r)
                    if p == parent and nm == name:
                        return data
                return None
            nxt = None
            for r in recs:
                p, nm, data = self.catkey(r)
                if self.cmpkey((p, nm), want) <= 0:
                    nxt = struct.unpack('>I', data[:4])[0]
                else:
                    break
            if nxt is None:
                return None
            i = nxt

    def children(self, parent):
        i = self.root
        while True:
            _, kind, _, recs = self.node(i)
            if kind == -1:
                break
            nxt = None
            for r in recs:
                p, nm, data = self.catkey(r)
                if self.cmpkey((p, nm), (parent, '')) < 0:
                    nxt = struct.unpack('>I', data[:4])[0]
                else:
                    break
            i = nxt if nxt is not None else struct.unpack('>I', self.catkey(recs[0])[2][:4])[0]
        out = []
        while i:
            flink, kind, _, recs = self.node(i)
            for r in recs:
                p, nm, data = self.catkey(r)
                if p == parent and nm:
                    out.append((nm, data))
                elif p > parent:
                    return out
            i = flink
        return out

    def overflow(self, fileid, ftype, startblock):
        out = []
        i = self.first
        while i:
            flink, kind, _, recs = self.node(i)
            for r in recs:
                klen, ft, _, fid, sb = struct.unpack('>HBBII', r[:12])
                if fid == fileid and ft == ftype:
                    d = r[2 + klen:]
                    out += [struct.unpack('>II', d[8 * k:8 * k + 8]) for k in range(8)]
            i = flink
        return [e for e in out if e[1]]


def main():
    img = Img(sys.argv[1])
    if '--no-replay' not in sys.argv:
        img.replay()
    print(img.replay_info, file=sys.stderr)
    fs = HFS(img)
    cmd, path = sys.argv[2], sys.argv[3]
    cnid, _ = fs.lookup(path)
    kids = fs.listdir(cnid)
    if cmd == 'ls':
        for nm, data in kids:
            rt = struct.unpack('>h', data[:2])[0]
            if rt == 2:
                size, ex = fs.fork_extents(fs.fork(data[88:168]), struct.unpack('>I', data[8:12])[0])
                print('%10d %3d ext  %s' % (size, len(ex), nm))
            elif rt == 1:
                print('%10s %3s      %s/' % ('-', '', nm))
    elif cmd == 'rget':
        outdir = sys.argv[4]
        nfiles = nbytes = 0
        def walk(cnid, dest):
            nonlocal nfiles, nbytes
            os.makedirs(dest, exist_ok=True)
            for nm, data in fs.listdir(cnid):
                rt = struct.unpack('>h', data[:2])[0]
                safe = nm.replace('/', ':')
                if safe in ('.', '..'):
                    continue
                if rt == 1:
                    walk(struct.unpack('>I', data[8:12])[0], os.path.join(dest, safe))
                elif rt == 2:
                    mode = struct.unpack('>H', data[32 + 10:32 + 12])[0]
                    if (mode & 0o170000) not in (0, 0o100000):
                        continue                      # symlinks, devices: skipped
                    buf = fs.read_fork(fs.fork(data[88:168]), struct.unpack('>I', data[8:12])[0])
                    with open(os.path.join(dest, safe), 'wb') as o:
                        o.write(buf)
                    nfiles += 1
                    nbytes += len(buf)
        walk(cnid, outdir)
        print('%d files, %d bytes' % (nfiles, nbytes))
    elif cmd == 'get':
        outdir = sys.argv[4]
        os.makedirs(outdir, exist_ok=True)
        names = sys.argv[5:]
        for nm, data in kids:
            rt = struct.unpack('>h', data[:2])[0]
            if rt != 2:
                continue
            names = [x for x in names if not x.startswith('--')]
            if names and not any(nm.startswith(x) for x in names):
                continue
            fid = struct.unpack('>I', data[8:12])[0]
            buf = fs.read_fork(fs.fork(data[88:168]), fid)
            with open(os.path.join(outdir, nm), 'wb') as o:
                o.write(buf)
            print('%10d %s' % (len(buf), nm))


if __name__ == '__main__':
    main()
