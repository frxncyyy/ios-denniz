#!/usr/bin/env python3
import pathlib,re,subprocess,tempfile
p=pathlib.Path(__file__).resolve().parent
root=p.parents[4]
src=root.parent/'nord2-current-kernel/drivers/misc/mediatek/connectivity/conninfra/platform/mt6893'
s=(src/'mt6893_pos.c').read_text().split('int connsys_spi_master_cfg_mt6893(')[1].split('\n//#ifndef')[0]
s=re.sub(r'/\*.*?\*/','',s,flags=re.S)
s=re.sub(r'if \(bt_only\) \{.*?\} else \{(.*?)\}',r'\1',s,flags=re.S)
defs=dict(re.findall(r'#define\s+(CONN_WT_SLP_CTL_REG_WB_\w+)\s+\(?\s*(0x[0-9a-fA-F]+)',(src/'include/mt6893_consys_reg_offset.h').read_text()))
expected=[]
for masked,name,v,m in re.findall(r'CONSYS_REG_WRITE(_MASK)?\(\s*CON_REG_WT_SPL_CTL_ADDR\s*\+\s*(\w+),\s*(0x[0-9a-fA-F]+)(?:,\s*(0x[0-9a-fA-F]+))?\s*\)',s):
 expected.append((0x18005000+int(defs[name],16),int(m,16) if masked else 0xffffffff,int(v,16)))
table=[tuple(int(x,16) for x in row) for row in re.findall(r'\{0x([0-9a-f]+)u,0x([0-9a-f]+)u,0x([0-9a-f]+)u\}',(p/'common.c').read_text())]
assert len(expected)==36 and table[4:]==expected
assert table[:4]==[(0x18000038,0xffffffff,0),(0x1800003c,0xffffffff,255),(0x18000050,0xffffffff,0),(0x18000054,0xffffffff,1)]
with tempfile.TemporaryDirectory() as d:
 out=pathlib.Path(d)/'test'
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-g',str(p/'common.c'),str(p/'test.c'),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)
print('PASS all36 table writes independently extracted from selected Android non-BT source')
