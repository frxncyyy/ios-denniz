#!/usr/bin/env python3
"""Read-only Mach-O inspector: slices, code signature (CD hash types, flags, identifier), entitlement keys,
encryption (cryptid), LC_LOAD_DYLIB list. Usage: machoinfo.py [--ents] [--dylibs] FILE..."""
import struct, sys, plistlib

CPU = {0x0100000c: 'arm64', 0x0000000c: 'arm', 0x01000007: 'x86_64'}
def sub(ct, st):
    if ct == 0x0100000c:
        s = st & 0xffffff
        return {0: 'arm64', 1: 'arm64v8', 2: 'arm64e'}.get(s, 'arm64?%d' % s) + ('(abi%d)' % ((st >> 24) & 0x3f) if s == 2 else '')
    return CPU.get(ct, hex(ct))

def slices(b):
    m = struct.unpack('>I', b[:4])[0]
    if m in (0xcafebabe, 0xcafebabf):
        n = struct.unpack('>I', b[4:8])[0]; out = []
        for i in range(n):
            if m == 0xcafebabe:
                ct, st, off, size, al = struct.unpack('>iiIII', b[8 + 20 * i:28 + 20 * i])
            else:
                ct, st, off, size, al, _ = struct.unpack('>iiQQII', b[8 + 32 * i:40 + 32 * i])
            out.append((ct & 0xffffffff, st & 0xffffffff, off, size))
        return out
    if b[:4] == b'\xcf\xfa\xed\xfe':
        ct, st = struct.unpack('<ii', b[4:12]); return [(ct & 0xffffffff, st & 0xffffffff, 0, len(b))]
    return None

def parse(b, want_ents, want_dylibs):
    ncmds, sizeofcmds = struct.unpack('<II', b[16:24]); filetype = struct.unpack('<I', b[12:16])[0]
    p = 32; info = {'type': filetype, 'cs': None, 'crypt': None, 'dylibs': []}
    for _ in range(ncmds):
        cmd, cs = struct.unpack('<II', b[p:p + 8])
        if cmd == 0x1d:
            off, size = struct.unpack('<II', b[p + 8:p + 16]); info['cs'] = (off, size)
        elif cmd == 0x2c:
            info['crypt'] = struct.unpack('<III', b[p + 8:p + 20])[2]
        elif cmd in (0xc, 0x80000018, 0x8000001f, 0x80000023):
            o = struct.unpack('<I', b[p + 8:p + 12])[0]; info['dylibs'].append(b[p + o:p + cs].split(b'\0')[0].decode())
        p += cs
    sig = {}
    if info['cs']:
        off, size = info['cs']; blob = b[off:off + size]
        magic, length, count = struct.unpack('>III', blob[:12])
        cds = []; ents = None; cms = 0
        for i in range(count):
            typ, o = struct.unpack('>II', blob[12 + 8 * i:20 + 8 * i])
            m, l = struct.unpack('>II', blob[o:o + 8])
            if m == 0xfade0c02:
                ver, flags, hoff, idoff = struct.unpack('>IIII', blob[o + 8:o + 24])
                ht = blob[o + 37]; ident = blob[o + idoff:o + l].split(b'\0')[0].decode(errors='replace')
                cds.append('slot%#x:ht%d:flags%#x:%s' % (typ, ht, flags, ident))
            elif m == 0xfade7171:
                ents = blob[o + 8:o + l]
            elif m == 0xfade0b01:
                cms = l - 8
        sig = {'cds': cds, 'cms_bytes': cms}
        if ents is not None:
            try:
                d = plistlib.loads(ents); sig['ents'] = d if want_ents else sorted(d)
            except Exception as e:
                sig['ents'] = 'unparseable %s' % e
    info['sig'] = sig
    if not want_dylibs: info.pop('dylibs')
    return info

def main():
    args = sys.argv[1:]; we = '--ents' in args; wd = '--dylibs' in args
    for f in [a for a in args if not a.startswith('--')]:
        b = open(f, 'rb').read(); sl = slices(b)
        if not sl: print(f, 'not Mach-O'); continue
        for ct, st, off, size in sl:
            mb = b[off:off + size]
            if mb[:4] != b'\xcf\xfa\xed\xfe': print(f, sub(ct, st), 'not 64-bit LE'); continue
            print(f, sub(ct, st), parse(mb, we, wd))

if __name__ == "__main__":
    main()
