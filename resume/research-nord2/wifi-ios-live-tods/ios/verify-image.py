#!/usr/bin/env python3
"""Verify an EXISTING live-ToDS image offline. Never builds an image or uses USB.

Only a private loader rebuild and kernel strip are performed, into --output.
This is artifact identity verification, not source/ISA review or DHCP proof.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import resource
import struct
import subprocess
import sys
import zlib

sys.dont_write_bytecode = True
P = Path(__file__).resolve().parent
R = next(p for p in P.parents if (p / 'AGENTS.md').is_file())
OLD = R / 'research/nord2/wifi-ios-live/ios'
CAPACITY, RESERVE, PAGE = 33554432, 0x200000, 2048
IMAGE = R / 'shim/out/nord2-xnu/nord2-ios-wifi-live-tods-stop23.img'
BASELINE_PAYLOAD = '96c717510604cb231f764d94bbc6f51cc0a650cb1ade485b23a28c4c69855562'
PINS = {
    'backup/nord2/boot.img': '04e84c4a14e5c3cb77d0e587da93b5a7e720e8c5b2a3a51ab3ef4cda746e1f01',
    'research/nord2/rescue/recompressed/base.img': 'ed05ea02d0499fe4109e6498a6852e769c346226aa4a56a220338673f8a2f5bf',
    'research/nord2/rescue/nord2-adb-rescue.img': 'd78bf9fe720a1d293b863ea0a8b30e0df4b5eafb097b1343a3c30e7c3d03bac5',
    'shim/out/nord2-xnu/nord2-ios-wifi-identity-keychain-stop23.img': 'd9a44316f6b257221a948a32483a6778b33cdf5057a3f79ffecfceebf0318de3',
    'research/nord2/wifi-ios-live/ios/manifest.json': '9b39ff3d039b16b9c9cd76d6acf3eeb549dbc283ff15f7c929d1c5890e09ea50',
    'research/nord2/wifi-ios-live/ios/payload.bin': BASELINE_PAYLOAD,
    'research/nord2/wifi/calibration/WIFI': '0000000000000000000000000000000000000000000000000000000000000000',
}
REBUILD_SOURCES = [
    'tools/nord2-build-loader.sh', 'tools/mkpayload.py', 'tools/adt.py',
    *['shim/nord2-xnu/' + n for n in ('start.S', 'loader.c', 'wifi_rom_fdt.c',
       'dma_pool_fdt.c', 'dma_pool_contract.c', 'wifi_rom_contract.h',
       'dma_pool_contract.h', 'xnu.ld')],
]


def need(condition, message):
    if not condition:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def file_sha(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def align(n, size):
    return (n + size - 1) & ~(size - 1)


def parts(data):
    """Independent Android-v2 parser; no packer/checker import."""
    need(len(data) >= PAGE and data[:8] == b'ANDROID!', 'Android header')
    u32 = lambda off: struct.unpack_from('<I', data, off)[0]
    need(u32(40) == 2 and u32(36) == PAGE, 'Android version/page')
    need(u32(12) == 0x40080000 and u32(20) == 0x51100000, 'load addresses')
    need(u32(24) == 0 and u32(1632) == 0, 'unexpected second/DTBO payload')
    sizes = (u32(8), u32(16), u32(1648))
    need(all(sizes), 'empty boot component')
    offsets = [PAGE]
    for size in sizes[:-1]:
        offsets.append(offsets[-1] + align(size, PAGE))
    end = offsets[-1] + align(sizes[-1], PAGE)
    need(end <= len(data), 'truncated Android component')
    return tuple(data[o:o + s] for o, s in zip(offsets, sizes)), offsets, sizes, end


def gunzip(data, maximum=128 * 1024 * 1024):
    dec = zlib.decompressobj(31)
    raw = dec.decompress(data, maximum + 1)
    need(len(raw) <= maximum and dec.eof and not dec.unconsumed_tail,
         'gzip truncated or exceeds fixed expansion bound')
    return raw, dec.unused_data


def payload_adt(payload):
    need(len(payload) >= 0x2a8 and payload[:8] == b'SWEETXNU', 'payload header')
    off, size = struct.unpack_from('<QQ', payload, 0x20)
    need(off >= 0x2a8 and size > 0 and off + size <= len(payload), 'ADT extent')
    return payload[off:off + size]


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def command(argv, log):
    with log.open('xb') as out:
        subprocess.run([str(x) for x in argv], cwd=R, stdout=out,
                       stderr=subprocess.STDOUT, check=True, timeout=180)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--check-existing', action='store_true', required=True)
    ap.add_argument('--build-result', type=Path, default=P / 'build-result.json')
    ap.add_argument('--source', required=True)
    ap.add_argument('--ci-run', type=int, required=True)
    ap.add_argument('--attempt', type=int, required=True)
    ap.add_argument('--kernel-sha256', required=True)
    ap.add_argument('--image-sha256', required=True)
    ap.add_argument('--image-bytes', type=int, required=True)
    ap.add_argument('--payload-sha256', required=True)
    ap.add_argument('--output', type=Path, required=True,
                    help='new directory under this stage/independent-image-review')
    args = ap.parse_args()
    need(not sys.flags.optimize, 'run without Python -O')
    resource.setrlimit(resource.RLIMIT_AS, (1 << 30, 1 << 30))
    need(re.fullmatch('[0-9a-f]{40}', args.source), 'full source SHA required')
    for value in (args.kernel_sha256, args.image_sha256, args.payload_sha256):
        need(re.fullmatch('[0-9a-f]{64}', value), 'full artifact SHA256 required')
    need(args.ci_run > 0 and args.attempt > 0 and 0 < args.image_bytes <= CAPACITY,
         'CI identity/image size')
    output = args.output.resolve()
    need(output.is_relative_to(P / 'independent-image-review') and not output.exists(),
         'output must be a NEW independent-image-review child directory')
    build_result = args.build_result.resolve()
    need(build_result == P / 'build-result.json', 'use this stage build result only')
    ci = R / f'research/ci-{args.ci_run}'
    kernel = ci / 'xnu-kernel/kernel.development.sweet'
    inputs = [*(R / p for p in PINS), *(R / p for p in REBUILD_SOURCES),
              Path(__file__).resolve(), P / 'build-image.py', build_result,
              P / 'payload.bin', P / 'baseline.adt', IMAGE, kernel,
              ci / 'run.json', kernel.parent / 'SHA256SUMS']
    tool_bin = R / '../toolchains/proton-clang/bin'
    strip = (R / '../toolchains/cctools/bin/aarch64-apple-darwin-strip').resolve()
    inputs += [strip, *(tool_bin / n for n in ('clang', 'ld.lld', 'llvm-objcopy'))]
    before = {str(p.relative_to(R)) if p.is_relative_to(R) else str(p):
              dict(bytes=p.stat().st_size, sha256=file_sha(p)) for p in inputs}
    for path, sha in PINS.items():
        need(before[path]['sha256'] == sha, 'historical/recovery pin: ' + path)
    for source in REBUILD_SOURCES:
        committed = subprocess.check_output(['git', 'show', args.source + ':' + source], cwd=R)
        need(digest(committed) == before[source]['sha256'], 'CI-source rebuild input: ' + source)
    run = json.loads((ci / 'run.json').read_text())
    need((run['status'], run['conclusion'], run['headSha'], run['databaseId'], run['attempt']) ==
         ('completed', 'success', args.source, args.ci_run, args.attempt), 'exact successful CI identity')
    checksums = [line.split()[0] for line in (kernel.parent / 'SHA256SUMS').read_text().splitlines()
                 if len(line.split()) == 2 and Path(line.split()[1].lstrip('*')).name == kernel.name]
    need(checksums == [args.kernel_sha256] and file_sha(kernel) == args.kernel_sha256,
         'CI kernel/checksum identity')
    result = json.loads(build_result.read_text())
    expected = dict(source_commit=args.source, ci_run=args.ci_run, ci_attempt=args.attempt,
                    kernel_path=str(kernel.relative_to(R)), kernel_sha256=args.kernel_sha256,
                    image=str(IMAGE.relative_to(R)), image_bytes=args.image_bytes,
                    image_sha256=args.image_sha256, payload_sha256=args.payload_sha256,
                    boot_spare_bytes=CAPACITY - args.image_bytes)
    need(all(result.get(k) == v for k, v in expected.items()), 'build-result artifact identity')
    need(result.get('hardware_tested') is False and result.get('dhcp_proven') is False,
         'offline result must not claim hardware or DHCP proof')
    need(IMAGE.stat().st_size == args.image_bytes and file_sha(IMAGE) == args.image_sha256,
         'final image identity')
    payload = (P / 'payload.bin').read_bytes()
    need(digest(payload) == args.payload_sha256, 'payload identity')
    old = (OLD / 'payload.bin').read_bytes()
    historical = json.loads((OLD / 'manifest.json').read_text())
    adt_blob = payload_adt(payload)
    need(adt_blob == payload_adt(old) == (P / 'baseline.adt').read_bytes(), 'executed ADT changed')
    need(digest(adt_blob) == historical['private_calibration_adt_sha256'] == result['baseline_adt_sha256'],
         'executed ADT hash')
    commandline = old[0x48:0x48 + 608]
    need(payload[0x48:0x48 + 608] == commandline, 'executed commandline changed')
    need(b'\0' in commandline, 'commandline NUL')
    text = commandline.split(b'\0', 1)[0].decode('ascii')
    need(commandline == text.encode().ljust(608, b'\0') and
         text == historical['commandline'] == result['commandline'], 'commandline padding/metadata')
    sys.path.insert(0, str(R / 'tools'))
    adt = load_module('live_tods_adt', R / 'tools/adt.py')
    tree, used = adt.parse(adt_blob)
    need(used == len(adt_blob) and adt.serialize(tree) == adt_blob, 'ADT parse/roundtrip')
    chosen = [node for node in tree.children if node.name == 'chosen']
    need(len(chosen) == 1, 'sole chosen node')
    calibration = (R / 'research/nord2/wifi/calibration/WIFI').read_bytes()
    need(len(calibration) == 6146 and any(calibration[4:10]) and not calibration[4] & 1,
         'calibration identity shape')
    need([v for k, _, v in chosen[0].props if k == 'nord2-wifi-calibration'] == [calibration],
         'retained calibration mismatch')
    need([(f, v) for k, f, v in chosen[0].props if k == 'mac-address-wifi0'] ==
         [(0, calibration[4:10])], 'retained factory identity mismatch')

    image = IMAGE.read_bytes()
    base = (R / 'research/nord2/rescue/recompressed/base.img').read_bytes()
    stock = (R / 'backup/nord2/boot.img').read_bytes()
    rescue = (R / 'research/nord2/rescue/nord2-adb-rescue.img').read_bytes()
    (kg, rd, dt), offsets, sizes, end = parts(image)
    (bkg, brd, bdt), _, _, _ = parts(base)
    (skg, _, sdt), _, _, _ = parts(stock)
    (_, rrd, rdt), _, _, _ = parts(rescue)
    need(end == len(image), 'unexpected image trailing content')
    for off, size in zip(offsets, sizes):
        need(not any(image[off + size:off + align(size, PAGE)]), 'Android component padding')
    blob, appended = gunzip(kg)
    linux, stock_append = gunzip(skg)
    base_linux, base_append = gunzip(bkg)
    need(linux == base_linux and appended == base_append == stock_append, 'Linux/appended DTS changed')
    need(dt == bdt == sdt == rdt and rd == brd, 'header DTS/rescue container changed')
    rescue_raw, extra = gunzip(rd, 64 * 1024 * 1024)
    original_rescue_raw, rescue_extra = gunzip(rrd, 64 * 1024 * 1024)
    need(not extra and not rescue_extra and rescue_raw == original_rescue_raw and
         digest(rescue_raw) == '95989a2ddff7d3fad8c63df9916a7d2510e09499f0b1710ac9fbf078ce60979f',
         'RAM ADB rescue contents changed')
    need(linux[0x38:0x3c] == b'ARM\x64', 'original Linux Image magic')
    linux_size = struct.unpack_from('<Q', linux, 16)[0]
    need(len(linux) <= linux_size <= 0x7000000, 'original Linux BSS extent')
    poff = align(RESERVE + linux_size, 0x4000)
    need(blob[RESERVE:RESERVE + len(linux)] == linux, 'packed Linux changed')
    need(not any(blob[RESERVE + len(linux):poff]), 'Linux BSS/alignment padding')
    need(blob[poff:] == payload, 'packed payload not stage payload')
    need(0x40080000 + len(blob) <= 0x51100000 and len(blob) <= RESERVE + 0x6000000,
         'loader source/ramdisk overlap')
    h = hashlib.sha1()
    for component in (kg, rd, b'', b'', dt):
        h.update(component)
        h.update(struct.pack('<I', len(component)))
    need(image[576:608] == h.digest().ljust(32, b'\0'), 'Android header ID')
    allowed = set(range(8, 12)) | set(range(1636, 1644)) | set(range(576, 608))
    need(all(a == b or i in allowed for i, (a, b) in enumerate(zip(image[:PAGE], base[:PAGE]))),
         'unexpected Android header mutation')
    need(struct.unpack_from('<Q', image, 1636)[0] == 0, 'nonzero DTBO offset')

    output.mkdir(parents=True)
    command(['sh', R / 'tools/nord2-build-loader.sh', output / 'loader'], output / 'loader.log')
    loader = (output / 'loader/loader-Image').read_bytes()
    need(64 <= len(loader) <= RESERVE and loader[56:60] == b'ARM\x64', 'rebuilt loader shape')
    need(len(loader) <= struct.unpack_from('<Q', loader, 16)[0] <= RESERVE, 'loader declared span')
    need(blob[:RESERVE] == loader.ljust(RESERVE, b'\0'), 'independently rebuilt loader mismatch')
    stripped = output / 'kernel.strip'
    command([strip, '-S', '-x', kernel, '-o', stripped], output / 'strip.log')
    fixed = bytearray(stripped.read_bytes())
    mkpayload = load_module('live_tods_macho', R / 'tools/mkpayload.py')
    macho = mkpayload.parse_macho(fixed)
    fixups = mkpayload.apply_chained_fixups(fixed, macho)
    segments = [seg for seg in macho['segs'] if seg['vmsize']]
    need(0 < len(segments) <= 64 and struct.unpack_from('<II', payload, 8) == (1, len(segments)),
         'payload Mach-O segment count')
    text_segment = macho['segs'][0]
    need(text_segment['name'] == '__TEXT' and text_segment['fileoff'] == 0, 'Mach-O text origin')
    va = text_segment['vmaddr'] & ~(0x2000000 - 1)
    kernel_end = align(max(s['vmaddr'] + s['vmsize'] for s in segments), 0x4000)
    phys, memory = mkpayload.adt_dram(adt_blob)
    adt_off, adt_size = struct.unpack_from('<QQ', payload, 0x20)
    need(struct.unpack_from('<7Q', payload, 0x10) ==
         (va, macho['entry'], adt_off, len(adt_blob), kernel_end, phys, memory), 'payload addresses')
    need((phys, memory) == (0x142000000, 0x3e000000), 'retained Nord2 DRAM window')
    cursor = 0x2a8 + 32 * len(segments)
    segment_pins = []
    for index, seg in enumerate(segments):
        actual = struct.unpack_from('<4Q', payload, 0x2a8 + 32 * index)
        need(actual == (seg['vmaddr'], seg['vmsize'], cursor, seg['filesize']), 'segment table/layout')
        data = fixed[seg['fileoff']:seg['fileoff'] + seg['filesize']]
        need(len(data) == seg['filesize'] and payload[cursor:cursor + len(data)] == data,
             'rebased Mach-O segment bytes')
        segment_pins.append(dict(name=seg['name'], bytes=len(data), sha256=digest(data)))
        cursor += len(data)
        need(not any(payload[cursor:align(cursor, 16)]), 'segment padding')
        cursor = align(cursor, 16)
    need(cursor == adt_off and adt_size == len(adt_blob) and
         len(payload) == align(adt_off + adt_size, 16) and
         not any(payload[adt_off + adt_size:]), 'ADT/trailing payload layout')
    after = {str(p.relative_to(R)) if p.is_relative_to(R) else str(p):
             dict(bytes=p.stat().st_size, sha256=file_sha(p)) for p in inputs}
    need(before == after, 'input changed during independent verification')
    report = dict(verdict='PASS', scope='offline artifact identity only', **expected,
                  baseline_image_sha256=historical['image_sha256'],
                  baseline_payload_sha256=BASELINE_PAYLOAD, adt_sha256=digest(adt_blob),
                  commandline_byte_exact=True, calibration_and_factory_identity_byte_exact=True,
                  linux_sha256=digest(linux), appended_dts_sha256=digest(appended),
                  header_dts_sha256=digest(dt), rescue_contents_sha256=digest(rescue_raw),
                  loader_sha256=digest(loader), kernel_segments=segment_pins, chained_fixups=fixups,
                  android_header_id_verified=True, recovery_d9a_and_ram_adb_hashes_verified=True,
                  inputs_immutable=True, inputs=before, hardware_tested=False, dhcp_proven=False,
                  authorization_granted=False, limitations=[
                      'Mach-O parsing/rebasing uses the source-pinned mkpayload decoder; packing and header/gzip verification are independent.',
                      'Loader is freshly rebuilt with source-pinned inputs using the existing loader toolchain.',
                      'No CI source-tree/ISA review, device access, runtime behavior or DHCP success is inferred.',
                      'No old-stage source/schema/reader approvals or authorization gates are imported.'])
    (output / 'report.json').open('x').write(json.dumps(report, indent=2) + '\n')
    artifacts = {str(p.relative_to(output)): dict(bytes=p.stat().st_size, sha256=file_sha(p))
                 for p in sorted(output.rglob('*')) if p.is_file()}
    (output / 'manifest.json').open('x').write(json.dumps(artifacts, indent=2) + '\n')
    print(json.dumps(dict(verdict='PASS', report=str(output / 'report.json'),
                         image_sha256=args.image_sha256, dhcp_proven=False)))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, KeyError, AssertionError, struct.error,
            subprocess.SubprocessError, zlib.error) as error:
        print('verification FAILED: ' + str(error), file=sys.stderr)
        sys.exit(1)
