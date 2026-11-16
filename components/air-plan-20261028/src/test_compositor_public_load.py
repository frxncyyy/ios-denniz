import hashlib
import subprocess
import tempfile
import unittest
from pathlib import Path
import sys

HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE))
import compositor_public_load_oracle as double_oracle

class PublicLoadProbeTests(unittest.TestCase):
    def test_draw_plan_selects_clear_then_load_and_distinct_texture_snapshots(self):
        with tempfile.TemporaryDirectory() as d:
            src=Path(d)/'plan.c'
            src.write_text('''#include <assert.h>\n#include "compositor_load_draw_plan.h"\nint main(void){struct tegu_public_draw_plan p={0};assert(!tegu_public_make_draw_plan(0,&p));assert(p.count==1&&p.action[0]==TEGU_PUBLIC_CLEAR_ACTION&&p.texture_snapshot[0]==0);assert(!tegu_public_make_draw_plan(1,&p));assert(p.count==2&&p.action[0]==TEGU_PUBLIC_CLEAR_ACTION&&p.action[1]==TEGU_PUBLIC_LOAD_ACTION&&p.texture_snapshot[0]==0&&p.texture_snapshot[1]==1);assert(tegu_public_make_draw_plan(1,0)<0);assert(tegu_public_byte_matches(100,100,0));assert(!tegu_public_byte_matches(101,100,0));assert(tegu_public_byte_matches(101,100,1));assert(!tegu_public_byte_matches(102,100,1));return 0;}\n''')
            exe=Path(d)/'plan'
            subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-I'+str(HERE),str(src),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_two_texture_alpha_sweep_half_samples_coverage_and_clear(self):
        expected_rows=[0,0]+[((1 << (13-y))-1)<<2 for y in range(2,13)]+[0,0,0]
        for y in range(16):
            self.assertEqual(sum(double_oracle.covered(x,y)<<x for x in range(16)),expected_rows[y])
        self.assertEqual(sum(row.bit_count() for row in expected_rows),66)
        self.assertEqual(double_oracle.source_texel(128,0),(64,32,16,128))
        self.assertEqual(double_oracle.source_texel(127,1),(15,63,31,127))
        self.assertEqual(double_oracle.frame(128)[5:],((84,72,76,192),(57,99,69,223)))
        hashes={}
        two_draw_sha={0:'e46da7ad77765f1ffb7fc57ecd1c7ae92b23eff4726efe703392a74b15152483',
            64:'1381bcbe63c7f0da23b59a9ed9cea83bfefbe5b59d6d851ce502d7e8db0ba8a3',
            128:'10d1618a27c35779984d8776e0f687f73ef85a4857051ee11829dc4c67b18732',
            192:'3df06d1fe06ef08d28c852708c1aaf5e750bd462b7bfea9cd289b8cb8ecfb5cc',
            255:'cef56e3b129341d163c28e6ac92228fcbff1bdea46c44f33ec754dc4e614ff8b'}
        one_draw_sha={0:'6158cfce5f70d87979cc805ddc2c284be6b3682e3875e47d51d0be3cfec168d3',
            64:'bfa579803ec66d33de95a359c8be4a55dcd715817d28084bcf05e74aa0a5054b',
            128:'2e5efa6bb5ba6d9aca8c2c62eb15ff96683e62b1792777d7b45cf27b69890622',
            192:'e3eb0ee46027f8274eabc2f5a9294d6fa18a3b93d587cfde356effd672d3d96e',
            255:'cef56e3b129341d163c28e6ac92228fcbff1bdea46c44f33ec754dc4e614ff8b'}
        for alpha in (0,64,128,192,255):
            with tempfile.TemporaryDirectory() as td:
                out=Path(td)/f'{alpha}.bgra';header=Path(td)/f'{alpha}.h'
                subprocess.run(['python3',str(HERE/'compositor_public_load_oracle.py'),'--alpha',str(alpha),'--output',str(out),'--header',str(header)],check=True,stdout=subprocess.DEVNULL)
                raw=out.read_bytes();self.assertEqual(len(raw),1024);self.assertTrue(header.exists())
                hashes[alpha]=hashlib.sha256(raw).hexdigest()
                self.assertEqual(hashes[alpha],two_draw_sha[alpha])
                self.assertEqual(hashlib.sha256(double_oracle.frame_once(alpha)).hexdigest(),one_draw_sha[alpha])
                mask,=double_oracle.frame(alpha)[1:2]
                self.assertEqual(sum(mask),66);self.assertEqual(256-sum(mask),190)
                clear=bytes((120,80,40,128));one_frame=double_oracle.frame_once(alpha)
                for pixel,inside in enumerate(mask):
                    if not inside:self.assertEqual(raw[4*pixel:4*pixel+4],clear)
                    if not inside:self.assertEqual(one_frame[4*pixel:4*pixel+4],clear)
        self.assertEqual(len(set(hashes.values())),5)
        for alpha in (0,64,128,192):
            self.assertNotEqual(double_oracle.frame(alpha)[0],double_oracle.frame_once(alpha))
        self.assertNotEqual(double_oracle.frame(128)[0],double_oracle.frame_same_source(128))
        # Source-over output gets a one-LSB tolerance only on the 66 covered
        # pixels; all 190 load-preserved clear pixels remain exact.
        self.assertEqual(double_oracle.frame(128)[4],(40,80,120,128))
        self.assertEqual(sum(not x for x in double_oracle.frame(128)[1]),190)

    def test_runtime_rebinds_texture_before_second_draw_and_checks_clear_exactly(self):
        source=(HERE/'compositor_public_probe.c').read_text()
        loop=source[source.index('for(uint32_t draw=0;draw<draw_plan.count;draw++)'):source.index('MSG0(encoder, "endEncoding")')]
        self.assertLess(loop.index('source_second'),loop.index('drawPrimitives:vertexStart:vertexCount:'))
        self.assertIn('source_second=MSG1(device,"newTextureWithDescriptor:"',source)
        self.assertIn('source_second, SEL("replaceRegion:mipmapLevel:withBytes:bytesPerRow:")',source)
        self.assertIn('source_second, SEL("getBytes:bytesPerRow:fromRegion:mipmapLevel:")',source)
        self.assertIn('tegu_load_covered_pixels[i/4]',source)
        self.assertIn('covered_pixels!=66||clear_pixels!=190||clear_mismatches',source)

    def test_default_probe_log_records_remain_byte_identical(self):
        source=(HERE/'compositor_public_probe.c').read_text()
        for line in (
            'TEGU_COMPOSITOR pso_compile_begin attachments=0:80/15,1:80/0,2-7:0/0 vertex=Float4@0,Float2@16,index0,stride40,PerVertex\\n',
            'TEGU_COMPOSITOR draw_submitted bindings=vs[0,2,3] fs[1,6] texture3 sampler0 offsets=16/16/24/30 rangeLengths=80/32/96/32 attachments=2\\n',
            'TEGU_COMPOSITOR verify pixels=256 byte_mismatches=%u cpu_readback_guard_mismatches=%u source_alpha=255\\n'):
            self.assertIn(line,source)

if __name__=='__main__':unittest.main()
