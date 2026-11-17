#!/usr/bin/env python3
"""Bounded ordinary LLVM/Mach-O static evidence; extraction is not a verdict."""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = next(p for p in HERE.parents if (p / 'shim/nord2-xnu').is_dir())
TOOLS = ROOT.parent / 'toolchains/proton-clang/bin'
SOURCE = '97c2150d0f7e77b2a00af18a9c5d081017b00652'
CI = 39684479974
FUNCTIONS = ['_nvc_run', '_nvc_capture', '_nvc_owner', '_nvc_read', '_nvc_write', '_nvc_rc', '_ncd_worker', '_nwm_failure_drain']
OPTIONAL = ['_nvc_after_persistent', '_nvc_observe', '_nvc_poll', '_nvc_stop', '_nvc_log', '_nvc_mark', '_nvc_now', '_ni_thread', '_ni_wifi_power_probe', '_nwfn_after_pool', '_ncb_after_basic', '_ncd_schedule', '_ni_pmic_read', '_nw_wait']
DATA = {'_nvc_addresses': 26, '_nvc_ops': 32, '_nvc_native': 136, '_nvc_phase': 4}

def digest(data):
    return hashlib.sha256(data).hexdigest()

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('kernel', type=Path)
    p.add_argument('--artifact-ready', action='store_true', required=True)
    p.add_argument('--expected-sha256', required=True)
    p.add_argument('--expected-bytes', type=int, required=True)
    a = p.parse_args()
    # Run only after parent explicitly confirms a complete, verified artifact.
    data = a.kernel.read_bytes()
    if len(data) != a.expected_bytes or digest(data) != a.expected_sha256:
        raise RuntimeError('kernel identity mismatch')
    if len(data) < 32 or struct.unpack_from('<I', data)[0] != 0xfeedfacf:
        raise RuntimeError('expected little-endian Mach-O64')
    ncmd, cmdbytes = struct.unpack_from('<II', data, 16)
    if ncmd > 256 or cmdbytes > len(data)-32:
        raise RuntimeError('load command bounds')
    cursor = 32
    segments = []
    text_ranges = []
    for _ in range(ncmd):
        if cursor+8 > 32+cmdbytes:
            raise RuntimeError('load command truncated')
        cmd, size = struct.unpack_from('<II', data, cursor)
        if size < 8 or cursor+size > 32+cmdbytes:
            raise RuntimeError('load command extent')
        if cmd == 0x19:
            if size < 72:
                raise RuntimeError('segment extent')
            name = data[cursor+8:cursor+24].split(b'\0')[0].decode('ascii')
            va, vs, off, fs = struct.unpack_from('<QQQQ', data, cursor+24)
            if off > len(data) or fs > len(data)-off:
                raise RuntimeError('segment file extent')
            segments.append({'name': name, 'address': va, 'virtual_bytes': vs,
                             'offset': off, 'file_bytes': fs})
            nsects = struct.unpack_from('<I', data, cursor+64)[0]
            if 72+80*nsects > size:
                raise RuntimeError('section extent')
            for index in range(nsects):
                sec = cursor+72+80*index
                secname = data[sec:sec+16].split(b'\0')[0]
                secva, secsize = struct.unpack_from('<QQ', data, sec+32)
                if secname == b'__text':
                    text_ranges.append((secva, secva+secsize))
        cursor += size
    def address(off):
        hit = [s['address']+off-s['offset'] for s in segments
               if s['offset'] <= off < s['offset']+s['file_bytes']]
        return hex(hit[0]) if len(hit) == 1 else None
    def read_vm(va, count):
        hit = [s for s in segments if s['address'] <= va
               and va-s['address']+count <= s['file_bytes']]
        if len(hit) != 1:
            raise RuntimeError('unmapped/ambiguous bounded symbol data')
        s = hit[0]
        off = s['offset']+va-s['address']
        return data[off:off+count], off
    raw = subprocess.check_output([str(TOOLS/'llvm-nm'), '-n', str(a.kernel)], text=True)
    syms = []
    for line in raw.splitlines():
        q = line.split()
        if len(q) == 3:
            try:
                syms.append((int(q[0], 16), q[1], q[2]))
            except ValueError:
                pass
    out = HERE/'artifact'
    out.mkdir(exist_ok=True)
    selected = {}
    for name in FUNCTIONS+OPTIONAL:
        # XNU symbols use Mach-O S/s, not ELF T/t. Section membership is
        # authority for function ranges; nm's display letter is insufficient.
        is_text = lambda va: any(start <= va < end for start, end in text_ranges)
        hits = [x for x in syms if x[2] == name and is_text(x[0])]
        if len(hits) != 1:
            if name in OPTIONAL:
                selected[name] = {'not_extracted': 'absent, duplicate or inlined; inspect caller'}
                continue
            raise RuntimeError('missing/duplicate function '+name)
        start = hits[0][0]
        ends = [x[0] for x in syms if x[0] > start and is_text(x[0])]
        if not ends:
            raise RuntimeError('unknown function end '+name)
        end = min(ends)
        if end-start > (65536 if name in ('_ni_thread','_ni_wifi_power_probe') else 16384):
            raise RuntimeError('function extent over bound '+name)
        text = subprocess.check_output([
            str(TOOLS/'llvm-objdump'), '-d', '--mattr=+lse', '--no-show-raw-insn',
            '--start-address='+hex(start), '--stop-address='+hex(end), str(a.kernel)
        ], text=True)
        f = out/(name[1:]+'.txt')
        f.write_text(text)
        selected[name] = {'address': hex(start), 'end': hex(end),
                          'sha256': digest(f.read_bytes())}
    constants = {}
    for name, count in DATA.items():
        hits = [x for x in syms if x[2] == name]
        if len(hits) != 1:
            constants[name] = {'not_extracted': 'missing/duplicate; inspect actual caller/data references'}
            continue
        va = hits[0][0]
        try:
            body, off = read_vm(va, count)
        except RuntimeError:
            if name not in ('_nvc_native', '_nvc_phase'):
                raise
            constants[name] = {'address': hex(va), 'bytes': count, 'not_extracted': 'zero-fill global; no initialized file bytes asserted'}
            continue
        constants[name] = {'address': hex(va), 'offset': hex(off), 'bytes': count,
                           'sha256': digest(body), 'hex': body.hex()}
        if name == '_nvc_addresses':
            values = list(struct.unpack('<13H', body))
            expected = [0x1be2,0x1be6,0x1be8,0x1bee,0x1bf4,0x1f98,0x1c08,0x1c0c,0x1c0e,0x1c14,0x1c1a,0x1f9c,0x18b4]
            if values != expected:
                raise RuntimeError('actual linked VCN33 address array mismatch')
            constants[name]['addresses'] = [hex(x) for x in values]
        if name == '_nvc_ops':
            constants[name]['function_pointers'] = [hex(x) for x in struct.unpack('<4Q', body)]
    (out/'constants.json').write_text(json.dumps(constants, indent=2)+'\n')
    result = {
        'status': 'EXTRACTED_NOT_YET_REVIEWED', 'ci_expected': CI,
        'source_expected': SOURCE, 'source_identity_from_ci_is_separate': True,
        'kernel': str(a.kernel.resolve()), 'bytes': len(data), 'sha256': digest(data),
        'functions': selected, 'segments': segments,
        'constants_sha256': digest((out/'constants.json').read_bytes()),
        'method': 'ordinary local LLVM/Mach-O static extraction; not REA Evidence or runtime observation'
    }
    (out/'extraction.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps({'functions': len(selected), 'sha256': result['sha256'],
                      'status': result['status']}))

if __name__ == '__main__':
    main()
