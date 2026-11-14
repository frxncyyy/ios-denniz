#!/usr/bin/env python3
"""erofs_get.py -- minimal read-only EROFS reader (no root, no erofs-utils on this host) to take one file out of an Android
partition image (odm/vendor), e.g. Android's aw8697_haptic_170.bin from the odm image streamed by tools/nord2-read-vendor.py.

Supports what the Nord 2 images use (vendor.img: "LZ4_0PADDING BIG_PCLUSTER"): compact + extended inodes, FLAT_PLAIN /
FLAT_INLINE files and directories, COMPRESSED_FULL / COMPRESSED_COMPACT (4B + 2B packs) LZ4 files with big pclusters.
Refuses (clear error) ztailpacking/fragments/chunked/interlaced layouts.  Format: linux fs/erofs/erofs_fs.h + zmap.c.

usage: erofs_get.py IMG --find NAME            list the paths of entries called NAME
       erofs_get.py IMG --get /path OUT          write that file to OUT
       erofs_get.py IMG --ls /dir
       erofs_get.py IMG --compare-tree DIR [N]   self-test: compare up to N regular files with an already extracted tree
"""
import mmap, os, struct, sys

SB_OFF = 1024
MAGIC = 0xE0F5E1E2
LI_D0_CBLKCNT = 1 << 11
ADV_COMPACTED_2B, ADV_BIG1, ADV_BIG2, ADV_INLINE_PCL, ADV_INTERLACED, ADV_FRAGMENT = 1, 2, 4, 8, 0x10, 0x20


def lz4_block(src, max_out):
    out = bytearray()
    i, n = 0, len(src)
    while i < n:
        tok = src[i]; i += 1
        lit = tok >> 4
        if lit == 15:
            while True:
                b = src[i]; i += 1; lit += b
                if b != 255:
                    break
        out += src[i:i + lit]; i += lit
        if i >= n or len(out) >= max_out:
            break
        off = src[i] | (src[i + 1] << 8); i += 2
        ml = tok & 15
        if ml == 15:
            while True:
                b = src[i]; i += 1; ml += b
                if b != 255:
                    break
        ml += 4
        if off == 0 or off > len(out):
            raise ValueError("lz4: bad offset")
        st = len(out) - off
        for k in range(ml):
            out.append(out[st + k])
        if len(out) >= max_out:
            break
    return bytes(out)


class Erofs:
    def __init__(self, path):
        self.f = open(path, "rb")
        self.m = mmap.mmap(self.f.fileno(), 0, access=mmap.ACCESS_READ)
        sb = self.m[SB_OFF:SB_OFF + 128]
        magic, = struct.unpack_from("<I", sb, 0)
        if magic != MAGIC:
            raise SystemExit(f"not EROFS (magic {magic:#x})")
        self.blkbits = sb[12]
        self.bs = 1 << self.blkbits
        self.root_nid, = struct.unpack_from("<H", sb, 14)
        self.meta_blkaddr, = struct.unpack_from("<I", sb, 40)
        self.incompat, = struct.unpack_from("<I", sb, 80)
        if self.incompat & ~0x3:
            raise SystemExit(f"unsupported EROFS incompat features {self.incompat:#x} (only 0PADDING / BIG_PCLUSTER)")

    def inode(self, nid):
        loc = self.meta_blkaddr * self.bs + nid * 32
        fmt, xic, mode = struct.unpack_from("<HHH", self.m, loc)
        ext = fmt & 1
        layout = (fmt >> 1) & 7
        if ext:
            size, = struct.unpack_from("<Q", self.m, loc + 8); isz = 64
        else:
            size, = struct.unpack_from("<I", self.m, loc + 8); isz = 32
        iu, = struct.unpack_from("<I", self.m, loc + 16)
        xsz = 0 if xic == 0 else 12 + (xic - 1) * 4
        return dict(loc=loc, isz=isz, xsz=xsz, mode=mode, size=size, layout=layout, iu=iu)

    def data(self, ino):
        lay, size = ino["layout"], ino["size"]
        if lay == 0:
            st = ino["iu"] * self.bs
            return bytes(self.m[st:st + size])
        if lay == 2:
            nblk = size // self.bs
            st = ino["iu"] * self.bs
            head = bytes(self.m[st:st + nblk * self.bs])
            tail_at = ino["loc"] + ino["isz"] + ino["xsz"]
            return head + bytes(self.m[tail_at:tail_at + size - nblk * self.bs])
        if lay in (1, 3):
            return self.zdata(ino)
        raise SystemExit(f"unsupported data layout {lay}")

    def zdata(self, ino):
        size = ino["size"]
        end = ino["loc"] + ino["isz"] + ino["xsz"]
        hpos = (end + 7) & ~7
        _r, advise, algo, cbits = struct.unpack_from("<IHBB", self.m, hpos)
        if advise & (ADV_INLINE_PCL | ADV_INTERLACED | ADV_FRAGMENT) or cbits & 0x80:
            raise SystemExit(f"unsupported compressed advise {advise:#x} / clusterbits {cbits:#x}")
        if (algo & 15) != 0:
            raise SystemExit(f"unsupported compression algorithm {algo & 15}")
        lbits = 12 + (cbits & 7)
        lsz = 1 << lbits
        total = (size + lsz - 1) >> lbits
        big = bool(advise & ADV_BIG1)
        idx = []          # (type, lo/clusterofs, pblk or None, cblk or None)
        if ino["layout"] == 1:
            base = hpos + 16
            for lcn in range(total):
                adv, cofs, u = struct.unpack_from("<HHI", self.m, base + lcn * 8)
                t = adv & 3
                if t == 2:
                    d0 = u & 0xFFFF
                    idx.append((t, d0, None, (d0 & ~LI_D0_CBLKCNT) if d0 & LI_D0_CBLKCNT else None))
                else:
                    idx.append((t, cofs, u, None))
        else:
            ebase = hpos + 8
            c4 = (32 - ebase % 32) // 4
            if c4 == 8:
                c4 = 0
            c2 = ((total - c4) // 16) * 16 if (advise & ADV_COMPACTED_2B) and c4 < total else 0
            for lcn in range(total):
                pos, l = ebase, lcn
                if l < c4:
                    ash = 2
                else:
                    pos += c4 * 4; l -= c4
                    if l < c2:
                        ash = 1
                    else:
                        pos += c2 * 2; l -= c2; ash = 2
                pos += l << ash
                idx.append(self.unpack_compacted(pos, ash, lbits, big))
        # heads -> extents
        heads = []
        for lcn, (t, lo, pblk, _c) in enumerate(idx):
            if t == 2:
                continue
            cblk = 1
            if big and lcn + 1 < total and idx[lcn + 1][0] == 2 and idx[lcn + 1][3] is not None:
                cblk = idx[lcn + 1][3]
            heads.append((lcn * lsz + lo, t, pblk, cblk))
        out = bytearray(size)
        for k, (ls, t, pblk, cblk) in enumerate(heads):
            le = heads[k + 1][0] if k + 1 < len(heads) else size
            if le <= ls:
                continue
            raw = bytes(self.m[pblk * self.bs:(pblk + cblk) * self.bs])
            if t == 0:
                piece = raw[:le - ls]
            else:
                z = 0
                while z < len(raw) and raw[z] == 0:
                    z += 1
                piece = lz4_block(raw[z:], le - ls)[:le - ls]
            if len(piece) != le - ls:
                raise SystemExit(f"extent {k}: got {len(piece)} bytes, want {le - ls}")
            out[ls:le] = piece
        return bytes(out)

    def unpack_compacted(self, pos, ash, lbits, big):
        vcnt = 2 if ash == 2 else 16
        psz = vcnt << ash
        lobits = max(lbits, 12)
        enc = ((psz - 4) * 8) // vcnt
        base = pos - (pos % psz)
        i = (pos - base) >> ash
        pack = bytes(self.m[base:base + psz + 4])

        def dec(j):
            bitpos = enc * j
            v, = struct.unpack_from("<I", pack, bitpos // 8)
            v >>= bitpos & 7
            return v & ((1 << lobits) - 1), (v >> lobits) & 3

        lo, t = dec(i)
        if t == 2:
            if lo & LI_D0_CBLKCNT:
                if not big:
                    raise SystemExit("CBLKCNT without big pcluster")
                return (2, 1, None, lo & ~LI_D0_CBLKCNT)
            return (2, lo, None, None)
        j, nblk = i, 0
        if not big:
            nblk = 1
            while j > 0:
                j -= 1
                l2, t2 = dec(j)
                if t2 == 2:
                    j -= l2
                if j >= 0:
                    nblk += 1
        else:
            while j > 0:
                j -= 1
                l2, t2 = dec(j)
                if t2 == 2:
                    if l2 & LI_D0_CBLKCNT:
                        j -= 1
                        nblk += l2 & ~LI_D0_CBLKCNT
                        continue
                    if l2 <= 1:
                        raise SystemExit("corrupted compacted index")
                    j -= l2 - 2
                    continue
                nblk += 1
        pb, = struct.unpack_from("<I", pack, psz - 4)
        return (t, lo, pb + nblk, None)

    def readdir(self, nid):
        ino = self.inode(nid)
        if (ino["mode"] & 0o170000) != 0o040000:
            raise SystemExit("not a directory")
        d = self.data(ino)
        ents = []
        for b0 in range(0, len(d), self.bs):
            blk = d[b0:b0 + self.bs]
            if len(blk) < 12:
                break
            first_nameoff, = struct.unpack_from("<H", blk, 8)
            n = first_nameoff // 12
            for k in range(n):
                cnid, noff, ft = struct.unpack_from("<QHB", blk, k * 12)
                nend = struct.unpack_from("<H", blk, (k + 1) * 12 + 8)[0] if k + 1 < n else len(blk)
                name = blk[noff:nend].split(b"\0", 1)[0].decode("utf-8", "replace")
                ents.append((name, cnid, ft))
        return ents

    def lookup(self, path):
        nid = self.root_nid
        for part in [p for p in path.split("/") if p]:
            m = [e for e in self.readdir(nid) if e[0] == part]
            if not m:
                raise SystemExit(f"{path}: '{part}' not found")
            nid = m[0][1]
        return nid

    def walk(self, nid=None, prefix=""):
        nid = self.root_nid if nid is None else nid
        for name, cnid, ft in self.readdir(nid):
            if name in (".", ".."):
                continue
            p = prefix + "/" + name
            yield p, cnid, ft
            if ft == 2:
                yield from self.walk(cnid, p)


def main():
    a = sys.argv[1:]
    if len(a) < 2:
        print(__doc__); sys.exit(2)
    fs = Erofs(a[0])
    if a[1] == "--find":
        for p, _n, ft in fs.walk():
            if os.path.basename(p) == a[2]:
                print(p)
    elif a[1] == "--ls":
        for name, cnid, ft in fs.readdir(fs.lookup(a[2])):
            print(ft, name)
    elif a[1] == "--get":
        ino = fs.inode(fs.lookup(a[2]))
        d = fs.data(ino)
        open(a[3], "wb").write(d)
        print(f"{a[2]}: {len(d)} bytes (layout {ino['layout']}) -> {a[3]}")
    elif a[1] == "--compare-tree":
        lim = int(a[3]) if len(a) > 3 else 200
        ok = bad = 0; lays = {}
        for p, cnid, ft in fs.walk():
            if ft != 1:
                continue
            ref = os.path.join(a[2], p.lstrip("/"))
            if not os.path.isfile(ref) or os.path.islink(ref):
                continue
            ino = fs.inode(cnid)
            if ino["size"] > 4 << 20:
                continue
            got = fs.data(ino)
            lays[ino["layout"]] = lays.get(ino["layout"], 0) + 1
            if got == open(ref, "rb").read():
                ok += 1
            else:
                bad += 1; print("MISMATCH", p, ino["layout"], ino["size"])
            if ok + bad >= lim:
                break
        print(f"compare: {ok} identical, {bad} different; layouts seen {lays}")
        sys.exit(1 if bad or not ok else 0)


if __name__ == "__main__":
    main()
