#!/usr/bin/env python3
"""Read-only, pinned selected-source constants and actual early observation checks."""
from pathlib import Path
import hashlib,json,re
R=Path(__file__).resolve().parents[5]
K=R.parent/'nord2-current-kernel'
H=K/'include/linux/mfd/mt6359p/registers.h'
h=H.read_text()
expected={'MT6359_LDO_VCN33_1_CON0':0x1be2,'MT6359_LDO_VCN33_1_MON':0x1be6,'MT6359_LDO_VCN33_1_OP_EN':0x1be8,'MT6359_LDO_VCN33_1_OP_EN_SET':0x1bea,'MT6359_LDO_VCN33_1_OP_CFG':0x1bee,'MT6359_LDO_VCN33_1_MULTI_SW':0x1bf4,'MT6359_VCN33_1_ANA_CON0':0x1f98,'MT6359_LDO_VCN33_2_CON0':0x1c08,'MT6359_LDO_VCN33_2_MON':0x1c0c,'MT6359_LDO_VCN33_2_OP_EN':0x1c0e,'MT6359_LDO_VCN33_2_OP_EN_SET':0x1c10,'MT6359_LDO_VCN33_2_OP_CFG':0x1c14,'MT6359_LDO_VCN33_2_MULTI_SW':0x1c1a,'MT6359_VCN33_2_ANA_CON0':0x1f9c}
for n,v in expected.items():
 m=re.search(r'^#define\s+'+n+r'\s+\(MT6359_PMIC_REG_BASE\+(0x[0-9a-f]+)\)',h,re.M);assert m and int(m[1],16)==v,n
for rail in (1,2):
 for field,v in [('EN_0_SHIFT',0),('EN_1_SHIFT',15)]:
  assert re.search(r'^#define\s+PMIC_RG_LDO_VCN33_'+str(rail)+'_'+field+r'\s+'+str(v)+r'\s*$',h,re.M)
 for field,v in [('VOSEL_MASK','0xF'),('VOSEL_SHIFT','8')]:
  assert re.search(r'^#define\s+PMIC_RG_VCN33_'+str(rail)+'_'+field+r'\s+'+v+r'\s*$',h,re.M)
reg=(K/'drivers/regulator/mt6359p-regulator.c').read_text()
for rail in (1,2):
 m=re.search(r'vcn33_'+str(rail)+r'_wifi_voltages\[\]\s*=\s*\{([^}]+)',reg)
 vals=[int(x) for x in re.findall(r'\b\d+\b',m[1])];assert len(vals)==16 and vals[13:]==[3300000,3400000,3500000]
log=R/'research/nord2/wifi-rx-lifecycle/ios/backend-first-receipt.log'
b=log.read_bytes();assert hashlib.sha256(b).hexdigest()=='465dabc7732177ad311a04b28dc3afdeaec5f4a413715c6187537967daf46dff'
s=b.decode();part=s.split('NORD2 WIFI: AP snapshot begin;',1)[1].split('NORD2 WIFI: snapshot complete',1)[0]
actual={int(a,16):int(v,0) for a,v in re.findall(r'PMIC\[(0x[0-9a-f]+)\]=(0x[0-9a-f]+|0)\b',part)}
for a,v in {0x1be2:0,0x1be6:0,0x1be8:0x8000,0x1bee:0,0x1bf4:0,0x1c08:0,0x1c0c:0,0x1c0e:0x8000,0x1c14:0,0x1c1a:0,0x1f98:0xf00,0x1f9c:0xf00}.items():assert actual[a]==v
assert int(re.search(r'rc=(0x[0-9a-f]+)',part)[1],16)&1
print(json.dumps({'result':'PASS','register_addresses':len(expected),'voltage_tables':2,'receipt_sha256':hashlib.sha256(b).hexdigest(),'observation':'single initial AP snapshot; no assertion about later rail state'},sort_keys=True))
