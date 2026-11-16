#!/usr/bin/env python3
"""Make a TeguMetal pipeline pack from pipeline cache files (pipeline_cache.c).

The inputs are k<kind>-<slot>.tpc files a TeguMetal process wrote into its
cache directory (com.tegu.metal.pipelines).  Only entries whose header
(format version, kind, length, payload SHA-256) verifies and whose build tag
equals --build's are packed: entries made by other compilers can never be
served.  The pack goes into the bundle with
build_compute_bundle.py --pipeline-pack (old flow) or, for the Nord2Metal
bundle, by gpu/tools/package_metal_addon.py --pipeline-pack (or --bundle with
no --out, which writes <bundle>/PipelinePack.tpcpack); the frontend consults it
after its memory and disk caches and compiles on any miss or mismatch.

The build id is the TEGU_PIPELINE_CACHE_BUILD string compiled into the bundle:
--bundle reads it from the Nord2Metal Mach-O itself, so the pack always matches
the binary that will load it.  Cache entries are keyed by that id, so a pack is
only useful for the exact build it was made from; every rebuild needs a new one.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
import zlib

FORMAT_VERSION = 2
PACK_VERSION = 1
HEADER = 96
# Per-kind payload bound (tegu_pc_kind_config.max_entry in pipeline_cache_frontend.inc).
MAX_ENTRY = {1: 2 << 20, 2: 2 << 20}
PACK_MAX_ENTRIES = 8192  # TEGU_PC_PACK_MAX_ENTRIES
BUILD_ID = re.compile(rb'nord2-arch9-[0-9a-f]{64}')


def key_str(value: bytes) -> bytes:
    # tegu_pc_key_str: u64 little-endian length (two u32) then the bytes.
    return struct.pack('<II', len(value) & 0xffffffff, len(value) >> 32) + value


def build_tag(build: str) -> bytes:
    return hashlib.sha256(key_str(build.encode())).digest()[:8]


def bundle_build_id(bundle: Path) -> str:
    """The cache build id compiled into a Nord2Metal bundle (dir) or Mach-O file."""
    exe = bundle / 'Nord2Metal' if bundle.is_dir() else bundle
    found = set(BUILD_ID.findall(exe.read_bytes()))
    if len(found) != 1:
        raise SystemExit(f'{exe}: expected exactly one nord2-arch9-<sha256> build id, found {len(found)}')
    return found.pop().decode()


def pack_tag(pack: bytes) -> bytes:
    if len(pack) < 32 or pack[:4] != b'TGPK':
        raise SystemExit('not a pipeline pack')
    return pack[16:24]


def read_pack(data: bytes, tag: bytes):
    """Entries {(kind,key): payload} of a pack made for tag; verifies every record."""
    if pack_tag(data) != tag:
        raise SystemExit('pack was made for another build')
    version, count, zero = struct.unpack_from('<III', data, 4)
    if version != PACK_VERSION or zero or not 0 < count <= PACK_MAX_ENTRIES:
        raise SystemExit('unsupported pack header')
    out = {}
    for i in range(count):
        kind, codec = struct.unpack_from('<II', data, 32 + 88 * i)
        key = data[40 + 88 * i:72 + 88 * i]
        offset, length = struct.unpack_from('<QQ', data, 72 + 88 * i)
        digest = data[88 + 88 * i:120 + 88 * i]
        payload = data[offset:offset + length]
        if codec > 1 or len(payload) != length or hashlib.sha256(payload).digest() != digest:
            raise SystemExit(f'pack record {i} is corrupt')
        if codec == 1:   # u64 raw length + zlib stream
            raw = zlib.decompress(payload[8:])
            if struct.unpack_from('<Q', payload)[0] != len(raw):
                raise SystemExit(f'pack record {i}: raw length mismatch')
            payload = raw
        out[(kind, key)] = payload
    return out


def read_entry(path: Path, tag: bytes):
    data = path.read_bytes()
    if len(data) < HEADER or data[:4] != b'TGPC':
        return None, 'not a cache file'
    version, kind, zero = struct.unpack_from('<III', data, 4)
    key = data[16:48]
    (length,) = struct.unpack_from('<Q', data, 48)
    digest, file_tag = data[56:88], data[88:96]
    if version != FORMAT_VERSION or zero or kind not in MAX_ENTRY:
        return None, f'format {version} kind {kind}'
    if file_tag != tag:
        return None, 'other build'
    payload = data[HEADER:]
    if len(payload) != length or length > MAX_ENTRY[kind] or hashlib.sha256(payload).digest() != digest:
        return None, 'length or checksum'
    return (kind, key, payload), None


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build', help='TEGU_PIPELINE_CACHE_BUILD of the bundle the pack is for')
    p.add_argument('--bundle-result', type=Path, help='build_compute_bundle.py result.json (gives the build id)')
    p.add_argument('--bundle', type=Path, help='Nord2Metal.bundle directory or its Mach-O: build id is read from the binary')
    p.add_argument('--merge', type=Path, action='append', default=[], help='earlier pack for the same build to union in')
    p.add_argument('--compress', action=argparse.BooleanOptionalAction, default=True, help='zlib-compress entries that shrink (default on; the frontend needs libz, always present on iOS)')
    p.add_argument('--out', type=Path, help='default with --bundle <dir>: <dir>/PipelinePack.tpcpack')
    p.add_argument('inputs', type=Path, nargs='*', help='cache directories or .tpc files (device com.tegu.metal.pipelines)')
    a = p.parse_args()
    build = a.build or (json.loads(a.bundle_result.read_text())['pipeline_cache_build'] if a.bundle_result else None)
    if a.bundle:
        found = bundle_build_id(a.bundle)
        if build and build != found:
            p.error(f'--build {build[:24]} differs from the id in {a.bundle} ({found[:24]})')
        build = found
        if not a.out and a.bundle.is_dir():
            a.out = a.bundle / 'PipelinePack.tpcpack'
    if not build:
        p.error('--build, --bundle-result or --bundle required')
    if not a.out:
        p.error('--out required')
    if not a.inputs and not a.merge:
        p.error('give cache directories/.tpc files or --merge packs')
    tag = build_tag(build)
    files = []
    for item in a.inputs:
        files += sorted(item.glob('k*-*.tpc')) if item.is_dir() else [item]
    entries, skipped = {}, {}
    for old in a.merge:
        entries.update(read_pack(old.read_bytes(), tag))
    for path in files:
        entry, why = read_entry(path, tag)
        if entry:
            entries[(entry[0], entry[1])] = entry[2]
        else:
            skipped[why] = skipped.get(why, 0) + 1
    if not entries:
        sys.exit(f'no entries for build {build[:16]} (skipped {skipped})')
    if len(entries) > PACK_MAX_ENTRIES:
        sys.exit(f'{len(entries)} entries exceed the pack bound {PACK_MAX_ENTRIES}')
    ordered = sorted(entries.items())
    stored = []
    for (kind, key), payload in ordered:
        codec = 0
        if a.compress:   # zlib when it helps: u64 raw length + stream (pipeline_cache.c inflates through libz)
            packed = struct.pack('<Q', len(payload)) + zlib.compress(payload, 9)
            if len(packed) < len(payload):
                payload, codec = packed, 1
        stored.append((kind, key, codec, payload))
    index, offset = b'', 32 + 88 * len(ordered)
    for kind, key, codec, payload in stored:
        index += struct.pack('<II', kind, codec) + key + struct.pack('<QQ', offset, len(payload)) + hashlib.sha256(payload).digest()
        offset += len(payload)
    header = b'TGPK' + struct.pack('<III', PACK_VERSION, len(ordered), 0) + tag + bytes(8)
    blob = header + index + b''.join(payload for _, _, _, payload in stored)
    a.out.write_bytes(blob)
    kinds = {k: sum(1 for (kk, _) in entries if kk == k) for k in (1, 2)}
    print(json.dumps({'pack': str(a.out), 'bytes': len(blob), 'entries': len(ordered), 'air': kinds[1], 'mali': kinds[2],
                      'build': build, 'skipped': skipped, 'compressed': sum(1 for e in stored if e[2]), 'sha256': hashlib.sha256(blob).hexdigest()}, indent=2))


if __name__ == '__main__':
    main()
