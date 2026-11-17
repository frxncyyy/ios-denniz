#!/usr/bin/env python3
"""OFFLINE selected SOC3_0 PHY TLV policy. No IO, transport, RF or MMIO."""
import hashlib,json,struct
from pathlib import Path
MAGIC=0x556789AA
P=Path(__file__).resolve().parent
class Refused(ValueError): pass
def require(ok,why):
 if not ok: raise Refused(why)
def digest(b):return hashlib.sha256(b).hexdigest()
def nvram_span(b):
 require(len(b)>=262 and b[256:260]==bytes([0,3,0,1]),'NVRAM control/header')
 total=256+int.from_bytes(b[260:262],'little');require(262<=total<=len(b)<=8192,'NVRAM bounds')
 pos=256;tags=[];seen=set()
 while pos<total:
  if not any(b[pos:total]):break
  require(pos+3<=total,'truncated tag header')
  tag=b[pos];n=int.from_bytes(b[pos+1:pos+3],'little');end=pos+3+n
  require(n and end<=total and tag not in seen,'tag bounds/duplicate')
  seen.add(tag);tags.append((tag,pos,end));pos=end
 require([t for t,_,_ in tags[:1]]==[0],'missing control')
 ids=[t for t,_,_ in tags];require(all(t in ids for t in [7,8,9,10]),'missing EPA/ELNA tags')
 k=ids.index(7);require(ids[k:k+4]==[7,8,9,10],'EPA/ELNA order')
 start,end=tags[k][1],tags[k+3][1];payload=b[start:end]
 if 21 in ids:
  q=tags[ids.index(21)];payload+=b[q[1]:q[2]]
 # gl_init zeros OnlyPreCal before copy; padding follows source alignment.
 padded=payload+bytes((-len(payload))%4)
 require(0<len(padded)<=512,'source would truncate; refuse partial calibration')
 require(b[243] in [0,1],'coant not boolean')
 return padded,{'declared_total':total,'span_start':start,'span_end_exclusive':end,'span_length':end-start,'tag21_present':21 in ids,'aligned_length':len(padded),'coant':b[243]}
def resolve_board(wifi,dt,policy=None):
 policy=policy or json.loads((P/'policy.json').read_text())
 require(digest(wifi)==policy['wifi_sha256'] and digest(dt)==policy['dt_sha256'],'board provenance mismatch')
 # This policy is specifically the selected absent-context source path.
 require(policy['connfem_policy']=='selected_getter_no_context' and b'connfem' not in dt.lower(),'connfem absence policy not established')
 nv,meta=nvram_span(wifi)
 # connfem_api.c memsets each full output before checking null global context.
 return nv,bytes(104),bytes(40),meta

def tlv(tag,b):return struct.pack('<HH',tag,len(b))+b
def build(nv,fem,laa,command=1,*,backup_current_boot_valid=False):
 require(command in [1,2],'unsupported CAL command')
 require(command!=2 or backup_current_boot_valid is True,'BACKUP requires separately validated current boot EMI restore')
 require(0<len(nv)<=512 and len(nv)%4==0,'NVRAM size/alignment')
 require(len(fem)==104 and len(laa)==40,'FEM/LAA ABI size')
 require(int.from_bytes(fem[4:8],'little')<=32 and int.from_bytes(laa[4:8],'little')<=8,'pin counts')
 require(all(fem[8+i*3+2] in [0,1] for i in range(int.from_bytes(fem[4:8],'little'))),'pin polarity')
 body=tlv(1,bytes([command,0,0,0]))+tlv(0,nv)+tlv(2,fem)+tlv(3,laa)
 return struct.pack('<IBBH',MAGIC,4,1,len(body))+body

def validate_command(payload,nv,fem,laa,command=1,*,backup_current_boot_valid=False):
 require(payload==build(nv,fem,laa,command,backup_current_boot_valid=backup_current_boot_valid),'PHY command payload mismatch')
 return {'init_command_id':8,'packet_type':0xa0,'s2d':'CMD_H2N','tag_order':[1,0,2,3],'payload_length':len(payload),'phy_payload_sha256':digest(payload),'RF_calibration_requested':command==1}

def response(payload,command,*,event_id,sequence,expected_sequence,emi_size):
 require(command in [1,2] and event_id==5 and 1<=expected_sequence<=255 and sequence==expected_sequence,'response identity/sequence')
 require(len(payload)==28,'response exact bounds')
 magic,count,version,n=struct.unpack_from('<IBBH',payload)
 tag,size=struct.unpack_from('<HH',payload,8)
 require((magic,count,version,n,tag,size)==(MAGIC,1,1,20,1,16),'response TLV framing')
 event,status,rsv,address,length,temp=struct.unpack_from('<BBHIII',payload,12)
 require(rsv==0 and event==(0x80|command),'response CAL event/reserved')
 require(status in ([0] if command==1 else [0,2]),'CAL failed or unexpected status')
 offset=address&0xffffff
 needs_capture=command==1 or status==2
 if needs_capture:require(isinstance(emi_size,int) and emi_size>0 and 0<length<=emi_size and offset<=emi_size-length,'calibration EMI bounds')
 return {'event':event,'status':status,'recalibrated':needs_capture,'emi_offset':offset,'emi_length':length,'temperature_raw':temp,'backup_copy_not_performed':True}
