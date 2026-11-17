#!/usr/bin/env python3
from pathlib import Path
import struct,json,subprocess,tempfile
import phy_action as p
ROOT=Path(__file__).resolve().parents[6]
def refuses(fn):
 try:fn()
 except p.Refused:return
 raise AssertionError('accepted invalid input')
policy=json.loads((Path(__file__).parent/'policy.json').read_text())
for name,sha in policy['source_files'].items():assert p.digest(Path(name).read_bytes())==sha
with tempfile.TemporaryDirectory() as tmp:
 base=ROOT.parent/'nord2-current-kernel/drivers/misc/mediatek/connectivity'
 subprocess.run(['clang','-target','arm64-apple-macos14','-ffreestanding','-Wall','-Werror','-I'+str(base/'wlan/core/gen4m/include'),'-I'+str(base/'wlan/core/gen4m/os/linux/include'),'-I'+str(base/'connfem/include'),'-c',str(Path(__file__).parent/'abi-test.c'),'-o',tmp+'/abi.o'],check=True)
w=(ROOT/'research/nord2/wifi/calibration/WIFI').read_bytes();dt=(ROOT/'dtb/nord2/live.dts').read_bytes()
nv,fem,laa,meta=p.resolve_board(w,dt)
assert meta=={'declared_total':4298,'span_start':3972,'span_end_exclusive':4197,'span_length':225,'tag21_present':False,'aligned_length':228,'coant':0}
assert nv[:225]==w[3972:4197] and nv[225:]==bytes(3) and fem==bytes(104) and laa==bytes(40)
b=p.build(nv,fem,laa);assert len(b)==400
summary=p.validate_command(b,nv,fem,laa)
for i in range(len(b)):
 bad=bytearray(b);bad[i]^=1;refuses(lambda:p.validate_command(bad,nv,fem,laa))
for n in range(len(b)):refuses(lambda:p.validate_command(b[:n],nv,fem,laa))
refuses(lambda:p.build(nv,fem,laa,2));refuses(lambda:p.resolve_board(w,dt+b' '))
refuses(lambda:p.resolve_board(w[:-1]+bytes([w[-1]^1]),dt))
for n in [0,1,255,261,4196,4297]:refuses(lambda:p.nvram_span(w[:n]))
bad=bytearray(w);bad[4049]=7;refuses(lambda:p.nvram_span(bad))
bad=bytearray(w);bad[3973:3975]=b'\xff\xff';refuses(lambda:p.nvram_span(bad))
bad=bytearray(fem);bad[4]=33;refuses(lambda:p.build(nv,bad,laa))
bad=bytearray(laa);bad[4]=9;refuses(lambda:p.build(nv,fem,bad))
def rsp(cmd=1,status=0,offset=0x100,length=0x800):
 return struct.pack('<IBBHHHBBHIII',p.MAGIC,1,1,20,1,16,0x80|cmd,status,0,offset,length,25)
def parse(b,cmd=1,**kw):return p.response(b,cmd,event_id=kw.get('eid',5),sequence=kw.get('seq',7),expected_sequence=7,emi_size=0x450000)
r=parse(rsp());assert r['recalibrated'] and r['backup_copy_not_performed']
assert not parse(rsp(2),2)['recalibrated'];assert parse(rsp(2,2),2)['recalibrated']
for status in [1,2,3,255]:refuses(lambda:parse(rsp(status=status)))
for eid in [0,1,4,6]:refuses(lambda:parse(rsp(),eid=eid))
refuses(lambda:parse(rsp(),seq=8));refuses(lambda:parse(rsp(2)))
for n in range(28):refuses(lambda:parse(rsp()[:n]))
refuses(lambda:parse(rsp()+b'\0'));refuses(lambda:parse(rsp(offset=0x44ffff,length=2)))
refuses(lambda:parse(rsp(length=0)))
(Path(__file__).parent/'board-metadata.json').write_text(json.dumps({'NVRAM':meta,'command':summary,'connfem':'source-backed absent-context getter output; not discovered physical FEM absence','pmic_coant_action_required':False,'pccif_on_operations':[{'address':'0x10003314','value':0},{'address':'0x10003314','value':1}]},indent=2)+'\n')
print('PASS: pinned private NVRAM/DT policy, exact225/228 extraction and400-byte CAL TLV; all400 one-byte mutations and truncations; provenance/order/size/backup refusal; strict response EID/sequence/status/EMI bounds. Offline only, no payload bytes/MAC logged.')
