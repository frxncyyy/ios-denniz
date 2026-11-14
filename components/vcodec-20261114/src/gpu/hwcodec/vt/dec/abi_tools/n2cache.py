#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Read the phone's dyld shared cache without any third-party code.

The cache directory is the user's own copy of the iOS 16.7.16 (20H392, iPhone X) shared cache.  The main file is a
Mach-O-like container whose mapping table tells which file offset backs each VM range; every non-empty subcache file
(dyld_shared_cache_arm64.NN, *.dylddata) has its own mapping table.  This module concatenates those tables, so
`Cache.rd(address, n)` reads any mapped byte of the cache.

Pointers in this cache are stored with fixup metadata in the high bits; addresses of interest are below 2^36, and
`fix()` masks to 36 bits (verified against every record and code pointer used by DEC08).

The text is read-only.  No file is written by this module.
"""
import glob
import mmap
import os
import re
import struct
import subprocess
import tempfile

DEFAULT_CACHE = '$HOME/dsc-work/sys/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64'
MASK36 = 0xFFFFFFFFF


def fix(v):
    """Decode a pointer as stored in this shared cache (low 36 bits; 0 stays 0)."""
    return v & MASK36 if v else 0


class Segment(object):
    __slots__ = ('name', 'vmaddr', 'vmsize', 'fileoff', 'sections')

    def __init__(self, name, vmaddr, vmsize, fileoff, sections):
        self.name = name
        self.vmaddr = vmaddr
        self.vmsize = vmsize
        self.fileoff = fileoff
        self.sections = sections


class Cache(object):
    def __init__(self, main=DEFAULT_CACHE):
        self.main = main
        self.files = []
        self.maps = []
        paths = [main] + sorted(p for p in glob.glob(main + '.*') if ':' not in os.path.basename(p))
        # A decompressed LINKEDIT subcache may be supplied through N2_DEC_ABI_LINKEDIT (path prefix .NN -> file).
        for p in paths:
            try:
                if os.path.getsize(p) < 0x200:
                    continue
                fh = open(p, 'rb')
                mm = mmap.mmap(fh.fileno(), 0, access=mmap.ACCESS_READ)
            except (OSError, ValueError):
                continue
            self.files.append((p, mm))
            mo, mc = struct.unpack('<II', mm[16:24])
            for i in range(mc):
                a, s, fo, _mp, _ip = struct.unpack('<QQQII', mm[mo + i * 32:mo + i * 32 + 32])
                self.maps.append((a, s, mm, fo))
        self.maps.sort(key=lambda x: x[0])
        self.main_map = self.files[0][1]
        io, ic = struct.unpack('<II', self.main_map[0x1c0:0x1c8])
        self.images = []
        for i in range(ic):
            a, _mt, _ino, po, _pad = struct.unpack('<QQQII', self.main_map[io + i * 32:io + i * 32 + 32])
            self.images.append((a, self.main_map[po:po + 300].split(b'\0')[0].decode('latin1')))

    # ---- raw access ---------------------------------------------------------------------------------------------
    def rd(self, addr, n):
        for base, size, mm, foff in self.maps:
            if base <= addr < base + size:
                off = foff + addr - base
                return bytes(mm[off:off + n])
        return b''

    def q(self, addr):
        b = self.rd(addr, 8)
        return struct.unpack('<Q', b)[0] if len(b) == 8 else 0

    def w(self, addr):
        b = self.rd(addr, 4)
        return struct.unpack('<I', b)[0] if len(b) == 4 else 0

    def ptr(self, addr):
        return fix(self.q(addr))

    def cs(self, addr, limit=200):
        b = self.rd(addr, limit)
        if not b:
            return None
        s = b.split(b'\0')[0]
        try:
            return s.decode('ascii')
        except UnicodeDecodeError:
            return None

    def cfstr(self, addr):
        b = self.rd(addr, 32)
        if len(b) < 32:
            return None
        flags = struct.unpack('<Q', b[8:16])[0]
        if flags & 0xffff not in (0x7c8, 0x7d0):
            return None
        return self.cs(fix(struct.unpack('<Q', b[16:24])[0]))

    # ---- images and segments ------------------------------------------------------------------------------------
    def image(self, suffix):
        for a, p in sorted(self.images):
            if p.endswith(suffix):
                return a, p
        raise KeyError('no image ending in ' + suffix)

    def image_end(self, addr):
        imgs = sorted(self.images)
        for i, (a, _p) in enumerate(imgs):
            if a == addr:
                return imgs[i + 1][0] if i + 1 < len(imgs) else a + 0x2000000
        return addr + 0x2000000

    def segs(self, base):
        h = self.rd(base, 32)
        if len(h) < 32:
            return []
        ncmds, sz = struct.unpack('<II', h[16:24])
        lc = self.rd(base + 32, sz)
        o = 0
        out = []
        for _ in range(ncmds):
            if o + 8 > len(lc):
                break
            cmd, cs = struct.unpack('<II', lc[o:o + 8])
            if cmd == 0x19:
                name = lc[o + 8:o + 24].split(b'\0')[0].decode('latin1')
                vm, vs, fo = struct.unpack('<QQQ', lc[o + 24:o + 48])
                ns = struct.unpack('<I', lc[o + 64:o + 68])[0]
                secs = []
                for k in range(ns):
                    so = o + 72 + k * 80
                    sn = lc[so:so + 16].split(b'\0')[0].decode('latin1')
                    sa, ss = struct.unpack('<QQ', lc[so + 32:so + 48])
                    secs.append((sn, sa, ss))
                out.append(Segment(name, vm, vs, fo, secs))
            o += cs
        return out

    def text(self, base):
        for s in self.segs(base):
            if s.name == '__TEXT':
                return s.vmaddr, self.rd(s.vmaddr, s.vmsize)
        raise KeyError('no __TEXT')

    # ---- names (optional; needs the decompressed symbol table) --------------------------------------------------
    def symbol_table(self, path):
        """Return sorted [(addr, name)] from a decompressed dyld symbols file."""
        with open(path, 'rb') as f:
            f.seek(16384)
            hdr = f.read(24)
            no, nc, so, ss, _eo, _ec = struct.unpack('<6I', hdr)
            base = 16384
            f.seek(base + no)
            nl = f.read(nc * 16)
            f.seek(base + so)
            st = f.read(ss)
        rows = []
        for k in range(nc):
            strx, _t, _se, _de, val = struct.unpack('<IBBHQ', nl[k * 16:k * 16 + 16])
            end = st.find(b'\0', strx)
            if end < 0:
                continue
            try:
                rows.append((val, st[strx:end].decode('latin1')))
            except UnicodeDecodeError:
                continue
        rows.sort()
        return rows

    def export_trie(self, image_suffix, linkedit_file):
        """Return [(name, addr)] from an image's export trie in a decompressed LINKEDIT subcache file."""
        a, _p = self.image(image_suffix)
        toff = tsize = None
        for s in self.segs(a):
            if s.name == '__LINKEDIT':
                for cmd in self._load_commands(a):
                    if cmd[0] == 0x80000033:
                        toff, tsize = struct.unpack('<II', cmd[1][:8])
        if toff is None:
            return []
        data = self.rd_linkedit(linkedit_file, toff, tsize)
        return self._walk_trie(data, a)

    def _load_commands(self, base):
        h = self.rd(base, 32)
        ncmds, sz = struct.unpack('<II', h[16:24])
        lc = self.rd(base + 32, sz)
        o = 0
        out = []
        for _ in range(ncmds):
            cmd, cs = struct.unpack('<II', lc[o:o + 8])
            out.append((cmd, lc[o + 8:o + cs]))
            o += cs
        return out

    @staticmethod
    def rd_linkedit(path, off, n):
        with open(path, 'rb') as f:
            f.seek(off)
            return f.read(n)

    @staticmethod
    def _walk_trie(t, base):
        def uleb(i):
            r = s = 0
            while True:
                c = t[i]
                i += 1
                r |= (c & 0x7f) << s
                s += 7
                if not c & 0x80:
                    return r, i
        out = []
        stack = [(0, '')]
        while stack:
            off, pre = stack.pop()
            if off >= len(t):
                continue
            n, i = uleb(off)
            if n:
                _fl, i = uleb(i)
                ad, i = uleb(i)
                out.append((pre, base + ad))
            i += n
            if i >= len(t):
                continue
            cnt = t[i]
            i += 1
            for _ in range(cnt):
                e = t.index(b'\0', i)
                s = t[i:e].decode('latin1')
                i = e + 1
                co, i = uleb(i)
                stack.append((co, pre + s))
        return out

    # ---- disassembly --------------------------------------------------------------------------------------------
    def disasm(self, addr, end=None, symbols=None):
        """llvm-objdump text for [addr, end), with symbol/string annotations from `symbols` (addr -> name)."""
        if end is None:
            end = addr + 0x400
        data = self.rd(addr, end - addr)
        tmpdir = tempfile.mkdtemp(prefix='n2decabi-', dir=os.environ.get('N2_TMP', '/tmp'))
        raw = os.path.join(tmpdir, 't.bin')
        obj = os.path.join(tmpdir, 't.o')
        with open(raw, 'wb') as f:
            f.write(data)
        subprocess.run(['clang', '-c', '--target=aarch64-linux-gnu', '-x', 'assembler', '-', '-o', obj],
                       input='.text\n$x:\n.incbin "%s"\n' % raw, text=True, check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        text = subprocess.run(['llvm-objdump', '-d', '--adjust-vma=%d' % addr, '--no-show-raw-insn', obj],
                              capture_output=True, text=True, check=True).stdout
        return self._annotate(text, addr, symbols or {})

    def _annotate(self, text, base, symbols):
        pend = {}
        out = []
        for line in text.splitlines():
            m = re.match(r'\s*([0-9a-f]+):\s+(.*)', line)
            if not m:
                continue
            pc = int(m.group(1), 16)
            ins = m.group(2).replace('\t', ' ').strip()
            note = ''
            ma = re.match(r'adrp\s+(x\d+), 0x([0-9a-f]+)', ins)
            if ma:
                pend[ma.group(1)] = int(ma.group(2), 16)
            mb = re.match(r'add\s+(x\d+), (x\d+), #0x([0-9a-f]+)$', ins)
            if mb and pend.get(mb.group(2)):
                v = pend[mb.group(2)] + int(mb.group(3), 16)
                d = symbols.get(v)
                if d is None:
                    c = self.cfstr(v)
                    d = '@"%s"' % c if c else (self.cs(v) and '"%s"' % self.cs(v))
                note = ' ; %s %s' % (hex(v), d or '')
                pend[mb.group(1)] = v
            mc = re.match(r'(bl|b)\s+0x([0-9a-f]+)', ins)
            if mc:
                t = int(mc.group(2), 16)
                if not (base <= t < base + 0x400000):
                    nm = symbols.get(t)
                    if nm:
                        note = ' ; ' + nm
            out.append('%x: %s%s' % (pc, ins, note))
        return '\n'.join(out)
