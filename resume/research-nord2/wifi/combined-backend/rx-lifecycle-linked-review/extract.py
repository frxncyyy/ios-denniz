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
SOURCE = '8d6960fb52a07aa381a93a36418b6e128a4f3518'
CI = 39678188048
FUNCTIONS = [
    '_ndf_body', '_ncb_prepare', '_ncb_build', '_ncd_worker', '_nct_run',
    '_nct_lease_window', '_ncd_normal_owner_window', '_nwfn_guard',
    '_ncd_run', '_nbc_run', '_ncr_pump', '_ncr_memory_range', '_nrd_capture',
    '_ncs_build', '_nwm_failure_drain', '_ncb_report', '_ncr_prepare',
    '_nct_prepare', '_nct_native_run', '_ncb_sleep',
]
OPTIONAL = ['_event_kind', '_drain', '_classify', '_ncd_report', '_nbc_report']
KEYS = [
    'ScreenOnBeaconTimeoutCount', 'BeaconTimoutFilterDurationMs', 'AdapScan',
    'DropPacketsIPV4Low', 'DropPacketsIPV4High', 'DropPacketsIPV6Low',
    'DropPacketsIPV6High', '2GTxMaskDPDOn', '2GTxPMinus1dbAtHighTemp',
    'TssiGroupBackupRestore', 'LdtBTONullHwLifeTime',
]

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
        if end-start > 16384:
            raise RuntimeError('function extent over16KiB '+name)
        text = subprocess.check_output([
            str(TOOLS/'llvm-objdump'), '-d', '--no-show-raw-insn',
            '--start-address='+hex(start), '--stop-address='+hex(end), str(a.kernel)
        ], text=True)
        f = out/(name[1:]+'.txt')
        f.write_text(text)
        selected[name] = {'address': hex(start), 'end': hex(end),
                          'sha256': digest(f.read_bytes())}
    constants = {'public_default_keys': {}, 'default_tables': []}
    for key in KEYS:
        needle = key.encode()+b'\0'
        positions = []
        offset = 0
        while True:
            offset = data.find(needle, offset)
            if offset < 0:
                break
            positions.append({'offset': hex(offset), 'address': address(offset)})
            offset += len(needle)
        if not positions:
            raise RuntimeError('missing public default key '+key)
        constants['public_default_keys'][key] = positions
    for va, kind, name in syms:
        if name == '_defaults' and kind not in 'tTUu':
            body, off = read_vm(va, 176)  # eleven immutable key/value pointer pairs
            constants['default_tables'].append({'address': hex(va), 'offset': hex(off),
                'bytes': 176, 'sha256': digest(body),
                'pointers': [hex(x) for x in struct.unpack('<22Q', body)]})
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
