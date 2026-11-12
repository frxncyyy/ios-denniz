#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Re-sign an ld64.lld-linked arm64 Mach-O (thin, with LC_CODE_SIGNATURE at the end of __LINKEDIT) with an ad-hoc
# CodeDirectory (0x20400, SHA-256, 4K pages) plus an XML entitlements blob in special slot 5 -- the same layout as
# ios-sweet/tools/mkinit.py's code_signature(), which this port's kernel accepts (sweet_amfi reads XML entitlements).
# usage: sign_ent.py MACHO ENTITLEMENTS.plist IDENTIFIER   (in place)
import hashlib, struct, sys
LC_SEGMENT_64, LC_CODE_SIGNATURE, PAGE_SHIFT = 0x19, 0x1d, 12
def code_signature(image, code_limit, text_size, ent, ident):
    page = 1 << PAGE_SHIFT
    hashes = b''.join(hashlib.sha256(image[o:min(o + page, code_limit)]).digest() for o in range(0, code_limit, page))
    nslots = len(hashes) // 32
    ent_blob = struct.pack('>II', 0xfade7171, 8 + len(ent)) + ent
    hashes = hashlib.sha256(ent_blob).digest() + bytes(32 * 4) + hashes
    hdr_len = 88; ident_off = hdr_len; hash_off = ident_off + len(ident) + 32 * 5
    cd_len = ident_off + len(ident) + len(hashes)
    cd = struct.pack('>9I4BI', 0xfade0c02, cd_len, 0x20400, 0x2, hash_off, ident_off, 5, nslots, code_limit, 32, 2, 0, PAGE_SHIFT, 0)
    cd += struct.pack('>III', 0, 0, 0) + struct.pack('>QQQQ', 0, 0, text_size, 1)
    assert len(cd) == hdr_len
    cd += ident + hashes
    sb_hdr = 12 + 8 * 2
    return (struct.pack('>3I', 0xfade0cc0, sb_hdr + len(cd) + len(ent_blob), 2) +
            struct.pack('>IIII', 0, sb_hdr, 5, sb_hdr + len(cd)) + cd + ent_blob)
def main():
    path, entp, name = sys.argv[1:4]
    data = bytearray(open(path, 'rb').read()); ent = open(entp, 'rb').read(); ident = name.encode() + b'\0'
    magic, _, _, _, ncmds, _, _, _ = struct.unpack_from('<IiiIIIII', data, 0); assert magic == 0xfeedfacf
    off = 32; cs = le = None; text_size = None
    for _ in range(ncmds):
        cmd, size = struct.unpack_from('<II', data, off)
        if cmd == LC_CODE_SIGNATURE: cs = off
        if cmd == LC_SEGMENT_64:
            nm = bytes(data[off + 8:off + 24]).rstrip(b'\0')
            if nm == b'__LINKEDIT': le = off
            if nm == b'__TEXT': text_size = struct.unpack_from('<Q', data, off + 48)[0]
        off += size
    assert cs and le and text_size, 'no LC_CODE_SIGNATURE/__LINKEDIT/__TEXT'
    dataoff, _ = struct.unpack_from('<II', data, cs + 8)
    image = data[:dataoff]
    size = (len(code_signature(bytes(image), dataoff, text_size, ent, ident)) + 15) & ~15
    struct.pack_into('<II', image, cs + 8, dataoff, size)
    le_fileoff = struct.unpack_from('<Q', image, le + 40)[0]
    struct.pack_into('<Q', image, le + 48, dataoff + size - le_fileoff)                     # filesize
    struct.pack_into('<Q', image, le + 32, (dataoff + size - le_fileoff + 0x3fff) & ~0x3fff)  # vmsize (16K pages)
    sig = code_signature(bytes(image), dataoff, text_size, ent, ident)
    out = bytes(image) + sig + bytes(size - len(sig))
    open(path, 'wb').write(out)
    print('signed', path, 'code_limit', dataoff, 'sig', size, 'entitlements', len(ent), 'bytes')
main()
