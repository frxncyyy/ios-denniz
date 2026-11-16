#!/usr/bin/env python3
"""Host test: make_pipeline_pack.py against the real pipeline_cache.c.

The C cache writes disk entries for build id B; the packer builds a pack from
them using the id read out of a fake Nord2Metal Mach-O (--bundle); a C driver
attaches the pack with build id B and must hit every entry, refuse the pack
under another id and report -2 for a missing file.  Also covers --merge,
the stale-pack guard helpers and a corrupted pack.  No device, no network.
"""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

gpu = Path(__file__).resolve().parents[1]
tegu = gpu / 'metal/tegu'
sys.path.insert(0, str(tegu))
import make_pipeline_pack as pk  # noqa: E402

BUILD = 'nord2-arch9-' + hashlib.sha256(b'unit test bundle').hexdigest()
OTHER = 'nord2-arch9-' + hashlib.sha256(b'another bundle').hexdigest()

DRIVER = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pipeline_cache.h"
static const struct tegu_pc_kind_config kinds[TEGU_PC_KINDS]={{0,0,0},{1u<<20,2u<<20,4096},{1u<<20,2u<<20,4096}};
static void key_of(unsigned i,unsigned char k[32]){struct tegu_pc_sha256 s;tegu_pc_sha256_init(&s);tegu_pc_key_u32(&s,i);tegu_pc_sha256_final(&s,k);}
static size_t payload_size(unsigned i){return 64+(i%50)*977;}
static void fill(unsigned i,unsigned char *p){for(size_t n=0;n<payload_size(i);n++)p[n]=(unsigned char)(i*31+n);}
int main(int argc,char **argv) {(void)argc;
 static struct tegu_pc c;unsigned char key[32],buf[65536],*got;size_t n;
 if(!strcmp(argv[1],"write")) { /* write <dir> <build> <first> <count> */
  tegu_pc_init(&c,argv[2],argv[3],kinds);unsigned first=atoi(argv[4]),count=atoi(argv[5]);
  for(unsigned i=first;i<first+count;i++){key_of(i,key);fill(i,buf);tegu_pc_store(&c,1+i%2,key,buf,payload_size(i));}
  return 0;
 }
 /* check <pack> <build> <first> <count>: every entry hits the pack, nothing else does */
 tegu_pc_init(&c,0,argv[3],kinds);int rc=tegu_pc_attach_pack(&c,argv[2]);
 if(!strcmp(argv[1],"refuse"))return rc==-1?0:10;
 if(!strcmp(argv[1],"absent"))return rc==-2?0:11;
 assert(!rc);unsigned first=atoi(argv[4]),count=atoi(argv[5]);
 for(unsigned i=first;i<first+count;i++) {
  key_of(i,key);fill(i,buf);
  assert(tegu_pc_lookup(&c,1+i%2,key,&got,&n)==3&&n==payload_size(i)&&!memcmp(got,buf,n));free(got);
 }
 key_of(first+count+7,key);assert(tegu_pc_lookup(&c,1,key,&got,&n)==0);
 printf("pack hit %u entries\n",count);return 0;
}
'''


def run(*args, check=True):
    result = subprocess.run([str(a) for a in args], capture_output=True, text=True)
    if check and result.returncode:
        sys.exit(f'FAIL {args[0]} {result.returncode}\n{result.stdout}{result.stderr}')
    return result


def main():
    with tempfile.TemporaryDirectory(prefix='pipeline-pack-test-') as tmp:
        work = Path(tmp)
        (work / 'driver.c').write_text(DRIVER)
        driver = work / 'driver'
        run('cc', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-I' + str(tegu),
            work / 'driver.c', tegu / 'pipeline_cache.c', '-o', driver)
        # Two "device runs" of the same build plus one of another build.
        for name, build, first, count in (('run1', BUILD, 0, 6), ('run2', BUILD, 6, 4), ('other', OTHER, 100, 3)):
            (work / name).mkdir()
            run(driver, 'write', work / name, build, first, count)
        # Fake bundle: the build id is a C string inside the Mach-O.
        bundle = work / 'Nord2Metal.bundle'
        bundle.mkdir()
        (bundle / 'Nord2Metal').write_bytes(b'\xcf\xfa\xed\xfe' + bytes(60) + BUILD.encode() + b'\0' + bytes(100))
        assert pk.bundle_build_id(bundle) == BUILD
        (work / 'two-ids').write_bytes(BUILD.encode() + b'\0' + OTHER.encode())
        try:
            pk.bundle_build_id(work / 'two-ids')
            raise AssertionError('ambiguous build id accepted')
        except SystemExit:
            pass
        # --bundle without --out writes into the bundle; entries of another build are skipped.
        result = run(sys.executable, tegu / 'make_pipeline_pack.py', '--bundle', bundle, work / 'run1', work / 'other')
        info = json.loads(result.stdout)
        pack = bundle / 'PipelinePack.tpcpack'
        assert info['entries'] == 6 and info['build'] == BUILD and info['skipped'] == {'other build': 3}, info
        out = run(driver, 'check', pack, BUILD, 0, 6)
        assert 'pack hit 6' in out.stdout
        # --merge unions a second run into the first pack.
        merged = work / 'merged.tpcpack'
        info = json.loads(run(sys.executable, tegu / 'make_pipeline_pack.py', '--build', BUILD, '--merge', pack,
                              '--out', merged, work / 'run2').stdout)
        assert info['entries'] == 10, info
        run(driver, 'check', merged, BUILD, 0, 10)
        data = merged.read_bytes()
        assert len(pk.read_pack(data, pk.build_tag(BUILD))) == 10
        # A pack never serves another build; a missing file is distinguishable.
        assert run(driver, 'refuse', merged, OTHER, 0, 0, check=False).returncode == 0
        assert run(driver, 'absent', work / 'missing.tpcpack', BUILD, 0, 0, check=False).returncode == 0
        assert pk.pack_tag(data) != pk.build_tag(OTHER)
        try:
            pk.read_pack(data, pk.build_tag(OTHER))
            raise AssertionError('pack of another build accepted')
        except SystemExit:
            pass
        # Corrupt payload: the tool refuses to merge it, the C loader drops the entry (checksum).
        bad = bytearray(data)
        bad[-3] ^= 0x41
        try:
            pk.read_pack(bytes(bad), pk.build_tag(BUILD))
            raise AssertionError('corrupt pack accepted')
        except SystemExit:
            pass
        # Nothing for this build: error, not an empty pack.
        failed = run(sys.executable, tegu / 'make_pipeline_pack.py', '--build', BUILD, '--out', work / 'x.tpcpack',
                     work / 'other', check=False)
        assert failed.returncode != 0 and not (work / 'x.tpcpack').exists()
    print('PASS pipeline pack tool: C-written cache files -> --bundle pack -> C attach hits, --merge, other-build/corrupt/absent refusals')


if __name__ == '__main__':
    main()
