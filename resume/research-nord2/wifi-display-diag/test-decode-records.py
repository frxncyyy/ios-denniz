from pathlib import Path
import importlib.util,re,subprocess,tempfile
p=Path(__file__).with_name('decode-records.py');spec=importlib.util.spec_from_file_location('decoder',p);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
root=Path(__file__).resolve().parents[3]
s=(root/'research/ci-39158169212/buildtree/xnu/iokit/Kernel/arm/SweetFramebuffer.cpp').read_text()
helper=s[s.index('static void\nsweet_fb_diag_record('):s.index('\nstatic uint32_t sweet_fb_swaps_logged;')]
with tempfile.TemporaryDirectory() as td:
 c=Path(td)/'test.c';exe=Path(td)/'test'
 c.write_text('#include <stdio.h>\n#include <stdint.h>\n#include <string.h>\n#define SWEET_FB_SWAP_REC_SIZE 0x46c\n#define IOLog printf\nstatic uint32_t get32(const uint8_t*p,unsigned o){uint32_t v;memcpy(&v,p+o,4);return v;}\n'+helper+'\nint main(void){uint8_t r[0x46c];for(unsigned i=0;i<sizeof(r);i++)r[i]=(i*37+11)&255;sweet_fb_diag_record(r,1);sweet_fb_diag_record(r,2);}\n')
 subprocess.run(['cc',str(c),'-o',str(exe)],check=True);raw=subprocess.check_output([str(exe)])
a=m.decode(raw);expected=bytes((i*37+11)&255 for i in range(m.SIZE)).hex()
assert not a['errors'] and all(r['complete'] and r['hex']==expected for r in a['records'])
rows=raw.splitlines(keepends=True);assert len(rows)==72
for bad in [b''.join(rows[1:]),raw[:-1],raw+rows[0],raw.replace(b'accepted=1',b'accepted=3',1),raw.replace(b'offset=0 words',b'offset=0x4 words',1),raw.replace(b'words=3',b'words=8',1)]:
 assert not all(r['complete'] for r in m.decode(bad)['records'])
assert all(not r['complete'] for r in m.decode(b'')['records'])
print('PASS: exact production helper output, complete reconstruction, missing/truncated/duplicate/bad-ID/offset/count rejection')
