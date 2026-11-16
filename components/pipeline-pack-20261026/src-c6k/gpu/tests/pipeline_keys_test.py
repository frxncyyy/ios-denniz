#!/usr/bin/env python3
"""Host test: pipeline_keys.py / pipeline_corpus.py against the real C key code and a device log.

1. Key primitives: a C driver built from pipeline_cache.c (tegu_pc_key_u32/bytes/str, SHA-256) hashes a
   fixed sequence; pipeline_keys.Key must give the same digest.
2. Replay record text round trip (record -> parse -> same key), constants replay grammar.
3. TEGU_PSO_REC chunk reassembly across interleaved processes, incomplete records counted.
4. Device vector (skipped when the private hardware artifacts are absent): c6jb luma_boost key
   c6d90ce8a16f4861 reproduced from its console trace, build id read from the installed Mach-O.
No device, no network, no compiler beyond one tiny clang call.
"""
import hashlib
import re
import subprocess
import sys
import tempfile
from pathlib import Path

gpu = Path(__file__).resolve().parents[1]
tegu = gpu / 'metal/tegu'
sys.path.insert(0, str(tegu))
import pipeline_corpus as pc  # noqa: E402
import pipeline_keys as pk  # noqa: E402

DRIVER = r'''
#include <stdio.h>
#include "pipeline_cache.h"
int main(void){
 struct tegu_pc_sha256 s;unsigned char d[32];tegu_pc_sha256_init(&s);
 tegu_pc_key_str(&s,"TPC-AIR-1");tegu_pc_key_u32(&s,0xdeadbeefu);
 const unsigned char blob[5]={1,2,3,4,5};tegu_pc_key_bytes(&s,blob,5);tegu_pc_key_bytes(&s,0,0);tegu_pc_key_str(&s,"");
 tegu_pc_sha256_final(&s,d);for(int i=0;i<32;i++)printf("%02x",d[i]);puts("");return 0;
}
'''


def test_primitives():
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / 'd.c'
        src.write_text(DRIVER)
        subprocess.run(['clang', '-O1', '-I' + str(tegu), str(src), str(tegu / 'pipeline_cache.c'), '-o', str(Path(tmp) / 'd')], check=True)
        c_digest = subprocess.run([str(Path(tmp) / 'd')], capture_output=True, text=True, check=True).stdout.strip()
    k = pk.Key().str('TPC-AIR-1').u32(0xdeadbeef).bytes(bytes([1, 2, 3, 4, 5])).bytes(b'').str('')
    assert k.digest().hex() == c_digest, (k.digest().hex(), c_digest)
    print('PASS key primitives equal the C implementation')


def sample():
    lib = 'ab' * 32
    consts = [pk.Constant(53, b'\x01', 'fc_generic'), pk.Constant(51, b'\x00\x01\x02', 'fc_color_opcodes0'), pk.Constant(49, b'\x07', None, 3)]
    vspec = pk.specialized_replay('vert', consts)
    fspec = pk.specialized_replay('frag', [])
    return pc.Pipeline(variant=4, format=0, vertex='vert', vlib=lib, vspec=vspec, fragment='frag', flib=lib, fspec=fspec,
                       targets=[(80, 15), (80, 0)], blends=[(1, 0, 0, 1, 0, 0, 1, 0, 15), (1, 0, 0, 1, 0, 0, 1, 0, 0)],
                       attrs=[(0, 31, 0, 1), (1, 29, 16, 1)], layouts=[(1, 48, 1, 1)], profile='cd' * 32, msgcap=512)


def test_records():
    p = sample()
    entry, parsed = pk.parse_replay(p.vspec)
    assert entry == 'vert' and [(c.kind, c.type, c.index, c.name, c.data) for c in parsed] == [
        (1, 53, 0, 'fc_generic', b'\x01'), (1, 51, 0, 'fc_color_opcodes0', b'\x00\x01\x02'), (0, 49, 3, None, b'\x07')], parsed
    assert pk.specialized_replay(entry, parsed) == p.vspec
    q, key = pc.parse_record(p.record('0' * 64))
    assert key == '0' * 64 and q.record('0' * 64) == p.record('0' * 64)
    profile = b'profile bytes'
    build = 'nord2-arch9-' + 'ef' * 32
    assert p.key(build, profile) == q.key(build, profile)
    assert p.key(build, profile) != p.key('nord2-arch9-' + '00' * 32, profile)
    # Different constants, different key; unspecialised (-) differs from specialised with no constants.
    r = sample()
    r.fspec = '-'
    assert r.key(build, profile) != p.key(build, profile)
    print('PASS replay record text round trip')


def test_console_chunks():
    text = sample().record('1' * 64)
    chunks = [text[i:i + 200] for i in range(0, len(text), 200)]
    n = len(chunks)
    lines = []
    for i, c in enumerate(chunks):
        lines.append('|backboardd| TEGU_PSO_REC k=%s src=miss i=%d/%d %s\n' % ('1' * 16, i + 1, n, c.replace(' ', '#')))
        lines.append('|SpringBoard| TEGU_PSO_REC k=%s src=disk i=%d/%d %s\n' % ('2' * 16, i + 1, n, c.replace(' ', '#')))
        lines.append('|SpringBoard| SWEET AKS: noise\n')
    lines = [l for l in lines if not ('k=' + '2' * 16 in l and 'i=%d/' % n in l)]  # drop the last chunk of the second record
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / 'log'
        path.write_text(''.join(lines))
        log = pc.Log(path)
    assert len(log.records) == 1 and log.records[0]['text'] == text and log.records[0]['src'] == 'miss' and log.incomplete_records == 1
    assert n > 1
    print('PASS TEGU_PSO_REC chunks reassemble per (process,key); incomplete record counted (%d chunks)' % n)


def test_device_vector():
    log = gpu / 'build/hardware-20261025/c6jb-test/ui-usb/xnu-live.log'
    macho = gpu / 'build/hardware-20261025/c6jb-test/offline-update/inputs/payload/Nord2Metal.bundle/Nord2Metal'
    if not (log.exists() and macho.exists() and pc.DEFAULT_FUNCTIONS.exists() and pc.DEFAULT_PROFILE.exists()):
        print('SKIP device vector (hardware artifacts absent)')
        return
    import make_pipeline_pack as mp
    build = mp.bundle_build_id(macho)
    profile, _ = pc.load_profile(pc.DEFAULT_PROFILE)
    if hashlib.sha256(profile).hexdigest() != '7b58347dca6fda6134f2ae81a8110ee59d9c42431cb5ae3f1264d8d119fd67a2':
        print('SKIP device vector (profile differs from the c6jb build)')
        return
    report, verified = pc.analyse([log], build)
    verdict = report[0]['verdicts']
    assert verdict['c6d90ce8a16f4861'] == 'verified', verdict
    assert sum(v == 'verified' for v in verdict.values()) >= 16, verdict
    print('PASS device vector: c6jb luma_boost key c6d90ce8a16f4861 and %d verified descriptions reproduced from the console log' %
          sum(v == 'verified' for v in verdict.values()))


if __name__ == '__main__':
    test_primitives()
    test_records()
    test_console_chunks()
    test_device_vector()
