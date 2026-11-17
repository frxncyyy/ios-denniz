#!/usr/bin/env python3
"""Captured-only combined backend receipt. No hardware or network authority."""
import argparse,hashlib,json,re
from pathlib import Path
LIMIT=8*1024*1024
SCHEMA={
'MAIN':'status stage ready halt error events finish',
'MAIN OWNER':'ap wf driver dma asleep balanced native_halt transport_halt',
'MAIN PRESPAN':'reason same sync valid native_before/2 span_native',
'MAIN PREORDER':'index events finish coex prepared',
'MAIN GUARD':'reason source stage error valid addr raw mask expected',
'MAIN NATIVE':'stage error halt live clock irq gic ap wf driver',
'MAIN INIT':'phase error halt dirty sched/3 reg memory idle',
'MAIN IRQ':'ena0/2 ena1/2 stat0/2 stat1/2 valid/2',
'MAIN TRANSPORT':'error halt submit events finish polls indices/2 bytes/3',
'MAIN TXDESC':'addr used didx valid mismatch wait_ddone consumed_zero',
'MAIN TXWB':'constructed observed ddone consumed_zero software_used',
'MAIN TXSUBMIT':'raw/4','MAIN TXOBSERVED':'raw/4',
'MAIN RXDESC':'addr buffer didx valid reason','MAIN RXWORDS':'raw/4',
'MAIN RXHEADER':'expected/2 type event/2 status cid valid',
'MAIN DMASTATUS':'raw/2 valid',
'MAIN SPANS':'stage completed next dirty halt words/2 remaps barriers error',
'MAIN SPANOBS':'target address expected observed ready raw polls',
'MAIN PHY':'attempted event backup halt rx fields eid response status',
'MAIN PHYBACKUP':'valid offset bytes copied reads halt error',
'CAP ARM':'cid eid seq deadline_us event_budget',
'CAP':'status phase complete halt error submit txdone events rearmed match polls',
'CAP RX':'bytes irq/2 valid debug echo mismatch elapsed_us',
'CAP DESCRIPTOR':'raw/4',
'CAP TLV':'elements known unknown mac_present mac_valid resources version product fw/2',
'CAP OWNER':'native_error native_halt init_halt driver dmairq/2 valid parked',
'CAP EVENT':'n kind type eid seq bytes declared/2 debug_type debug_bytes',
'CAP EVENTHEADER':'n valid bytes inner raw/9',
'CAP WORKER':'parking retained callbacks_complete',
'BASIC ARM':'cid seq bytes deadline_us rx_budget',
'BASIC':'status phase complete skipped reason halt error submit txdone polls debug rearmed callbacks publications',
'BASIC TX':'cid seq bytes from to completed didx control consumed ack_expected firmware_ack',
'BASIC TXDESC':'observed address expected software_used raw/4',
'BASIC RESOURCE':'valid version pse/2 ple/2 pages/2 extra debit/2 reserved remaining',
'BASIC CAPS':'valid sku efuse country phy band5 bw nss path known unknown',
'BASIC DEBUG':'valid index kind type eid seq bytes declared/2 inner id format length',
'BASIC DEBUGHEADER':'valid raw/9',
'BASIC OWNER':'worker native_error native_halt init_halt transport_halt firmware_halt completed lease',
'BASIC WORKER':'parking retained callbacks_complete',
'BACKEND ARM':'commands channels passive nvram',
'NORMAL':'status phase prepared halt error refusal cursor count complete current_cid events scan_complete service_timed_out event_limit callbacks pumps',
'NORMAL CREDIT':'pse_total basic_spent capacity available reserved acquired released ple_debit firmware_ack',
'NORMAL BOUNDS':'started last deadline command_deadline service_deadline command_us event_limit pump_limit',
'NORMAL COMMAND':'n cid seq bytes from to didx polls attempted published released txdone observed raw/4',
'BACKEND':'status attempted prepared refusal commands channels nvram interface association',
'RX':'entered halt error packets rearmed dropped malformed events debug sleepy unknown msdu reports phase queue',
'SCAN':'requested sequence done timed_out partial bss frames dropped state completed_channels pending unrelated drain',
'RXQUEUE':'n next didx packets rearmed waits segmented dropped raw/4',
'RXEVENT':'n bytes outer inner eid seq kind status',
}
REPEATED={'CAP EVENT','CAP EVENTHEADER','NORMAL COMMAND','RXQUEUE','RXEVENT'}
MAIN_KEYS=[x for x in SCHEMA if x.startswith('MAIN')]
MAIN_AUX={
'ARM':'stage action events finish ap wf driver',
'SPAN ARM':'index stage finish events coex',
'PCCIF ARM':'addr first second cal_emi/2',
'FINISH ARM':'spans cal coex cid',
}
N=r'(?:0x[0-9a-fA-F]+|[0-9]+)'
TOKEN=re.compile(r'([a-zA-Z0-9_]+)=('+N+r'(?:/'+N+r')*|-?[0-9]+|[a-z][a-z0-9-]*)')
TIME={'started','last','deadline','command_deadline','service_deadline'}
def decode(body,key,schema):
 prefix='NORD2 WIFI '+key+': '
 if not body.startswith(prefix):raise ValueError('record prefix')
 tokens=body[len(prefix):].split(' ')
 specs=schema.split()
 if len(tokens)!=len(specs):raise ValueError('field count')
 row={}
 for token,spec in zip(tokens,specs):
  name,_,arity=spec.partition('/');arity=int(arity or 1)
  if not token.startswith(name+'='):raise ValueError('field order/name')
  value=token[len(name)+1:]
  if name=='action':
   if value not in ('prepare','fresh-wake-and-wfdma','patch-copy','patch-finish','force-calibration','pccif','n9-copy','n9-finish-and-ready'):raise ValueError('action')
   row[name]=value;continue
  vals=value.split('/')
  if len(vals)!=arity:raise ValueError('field arity')
  nums=[]
  for val in vals:
   if len(val)>22 or not re.fullmatch('-?[0-9]+' if name=='status' else N,val):raise ValueError('numeric syntax')
   number=int(val,16 if val.startswith('0x') else 10)
   bound=64 if name in TIME or name in ('address','addr') else 32
   if not (-(1<<31)<=number<(1<<31) if name=='status' else 0<=number<(1<<bound)):raise ValueError('numeric width')
   nums.append(number)
  row[name]=nums[0] if arity==1 else tuple(nums)
 return row

def parse(data):
 result=dict(complete=False,terminal_receipt_valid=False,classification='incomplete_or_conflicting',backend_config_transferred=False,scan_complete=False,scan_done_observed=False,partial_cache=False,timed_out=False,bss_count=0,firmware_ack=False,interface_available=False,association_available=False,network_ready=False,retained=False,issues=[],sha256=hashlib.sha256(data).hexdigest())
 issues=result['issues']
 def ck(ok,why):
  if not ok and len(issues)<64:issues.append(why)
 if len(data)>LIMIT:ck(False,'input>8MiB');return result
 try:text=data.decode('utf8')
 except UnicodeDecodeError:ck(False,'invalid UTF8');return result
 rows={};positions={};ordered=[];aux=[]
 for lineno,line in enumerate(text.splitlines(keepends=True)):
  at=line.find('NORD2 WIFI ')
  if at<0:continue
  body=line[at:].rstrip('\r\n')
  if body.startswith('NORD2 WIFI MAIN DRAIN:'):ck(False,'common failure drain present');continue
  if not any(body.startswith('NORD2 WIFI '+f) for f in ['MAIN','CAP','BASIC','NORMAL','BACKEND','RX','SCAN']):continue
  if not line.endswith('\n') or len(body)>511 or line.count('NORD2 WIFI ')!=1:ck(False,'truncated/interleaved record');continue
  key=body[11:].split(':',1)[0]
  if key.startswith('MAIN ') and key[5:] in MAIN_AUX:
   try:decode(body,key,MAIN_AUX[key[5:]])
   except ValueError:ck(False,'malformed MAIN auxiliary')
   aux.append(lineno);continue
  if key not in SCHEMA:ck(False,'unknown record');continue
  try:row=decode(body,key,SCHEMA[key])
  except ValueError:ck(False,'malformed '+key);continue
  if key in rows and key not in REPEATED:ck(False,'duplicate '+key)
  rows.setdefault(key,[]).append(row);positions.setdefault(key,[]).append(lineno);ordered.append(key)
 required=set(SCHEMA)-REPEATED
 ck(required<=rows.keys(),'missing required record set')
 if not required<=rows.keys():return result
 def r(key):return rows[key][0]
 def eq(key,**want):ck(all(r(key).get(k)==v for k,v in want.items()),key+' contradiction')
 def exact(key,want):ck(tuple(r(key).values())==want,key+' contradiction')
 def before(a,b):ck(bool(positions.get(a)) and bool(positions.get(b)) and positions[a][-1]<positions[b][0],a+' order '+b)
 # MAIN critical success independently checked; no imported legacy parser.
 exact('MAIN',(0,9,1,0,0,3,2));exact('MAIN OWNER',(1,1,1,1,0,0,0,0))
 exact('MAIN PRESPAN',(0,0,1,1,(0,0),0));exact('MAIN PREORDER',(6,3,2,1,1));ck(not any(r('MAIN GUARD').values()),'MAIN first guard failure')
 eq('MAIN NATIVE',stage=6,error=0,halt=0,live=31,irq=1,gic=0,ap=1,wf=1,driver=1);ck(r('MAIN NATIVE')['clock']&0x4006==0x4006,'MAIN clock domain')
 eq('MAIN INIT',phase=5,error=0,halt=0,dirty=1,sched=(1,99,0));ck(r('MAIN INIT')['reg']>0 and r('MAIN INIT')['memory']>0,'MAIN initialized accesses')
 exact('MAIN IRQ',((0,0),(0,0),(0,0),(0,0),(3,3)))
 eq('MAIN TRANSPORT',error=0,halt=0,submit=0,events=3,finish=2,indices=(3,3),bytes=(36,36,12));ck(1<=r('MAIN TRANSPORT')['polls']<=3000,'MAIN transport polls')
 exact('MAIN TXDESC',(0x90000020,72,3,3,0,0,1));exact('MAIN TXWB',(1,1,1,1,72))
 exact('MAIN TXSUBMIT',((0x90005000,0x40480000,0,0),));exact('MAIN TXOBSERVED',((0x90005000,0x80000000,0,0),))
 exact('MAIN RXDESC',(0x90004020,0x90008000,3,31,0));exact('MAIN RXWORDS',((0x90008000,0xc0240000,0,0),))
 eq('MAIN RXHEADER',expected=(1,3),event=(1,3),status=0,valid=31);ck(r('MAIN RXHEADER')['type'] in (7,0xe000) and r('MAIN RXHEADER')['cid']<=255,'MAIN INIT response')
 eq('MAIN DMASTATUS',valid=3);ck(not any(x&0x00300000 for x in r('MAIN DMASTATUS')['raw']),'MAIN coherent status');ck(not (r('MAIN DMASTATUS')['raw'][1]&0x20000000),'MAIN pending STA29')
 eq('MAIN SPANS',stage=8,completed=127,next=7,dirty=1,halt=0,error=0);ck(r('MAIN SPANS')['words'][0]==r('MAIN SPANS')['words'][1]>0,'MAIN span words')
 eq('MAIN SPANOBS',target=0x404000,address=0x8ff025ec,expected=0x102d7cd4,observed=0x102d7cd4,ready=1);ck(r('MAIN SPANOBS')['raw']&3==3,'MAIN ready bits');ck(r('MAIN SPANOBS')['expected']==r('MAIN SPANOBS')['observed'] and r('MAIN SPANOBS')['polls']>0,'MAIN ready observation')
 exact('MAIN PHY',(1,1,1,0,60,1,5,0x81,0));eq('MAIN PHYBACKUP',valid=1,offset=0x2fa008,bytes=30720,copied=30720,reads=8,halt=0,error=0)
 ck([x for x in ordered if x.startswith('MAIN')]==MAIN_KEYS,'MAIN summary source order');before('MAIN PHYBACKUP','CAP ARM')
 # CAP source parity: headers pair exactly; inner16bit is captured opaque for
 # known outer software events, including updated debug behavior.
 exact('CAP ARM',(0x8a,0xec,4,3000000,16));eq('CAP',status=0,phase=5,complete=1,halt=0,error=0,submit=1,txdone=1,match=1)
 cap=r('CAP');ce=rows.get('CAP EVENT',[]);ch=rows.get('CAP EVENTHEADER',[])
 ck(1<=cap['events']<=16 and len(ce)==len(ch)==cap['events']==cap['rearmed'] and cap['events']+1<=cap['polls']<=3000,'CAP bounded events')
 eq('CAP RX',valid=3,debug=cap['events']-1);ck(r('CAP RX')['elapsed_us']<3000000 and not any(x&0x00300000 for x in r('CAP RX')['irq']),'CAP IRQ/deadline');ck(not (r('CAP RX')['irq'][1]&0x20000000),'CAP pending STA29')
 eq('CAP OWNER',native_error=0,native_halt=0,init_halt=0,driver=1,valid=3,parked=0);ck(not any(x&0x00300000 for x in r('CAP OWNER')['dmairq']),'CAP coherent owner');ck(not (r('CAP OWNER')['dmairq'][1]&0x20000000),'CAP owner pending STA29')
 eq('CAP DESCRIPTOR',raw=(0x90006000+((2+cap['events'])%16)*4096,3840<<16,0,0))
 tlv=r('CAP TLV');ck(tlv['elements']==tlv['known']+tlv['unknown'] and 2<=tlv['known']<=6 and tlv['mac_present'] in (0,1) and tlv['mac_valid']<=tlv['mac_present'] and tlv['resources']==1 and tlv['version']==0x80000001,'CAP TLV metadata');ck(tlv['elements']<=(r('CAP RX')['bytes']-40)//8 and 40+8*tlv['elements']+4*tlv['known']+4*tlv['mac_present']+24<=r('CAP RX')['bytes'],'CAP TLV bounded body');ck(tlv['product']<=65535 and all(x<=65535 for x in tlv['fw']),'CAP product/firmware widths')
 for j,(e,h) in enumerate(zip(ce,ch)):
  ck(e['n']==h['n']==j and h['valid']==1 and h['bytes']==36 and h['inner']<=65535 and e['type']<=65535 and e['type']&0x380f==0x3800 and e['seq']<=255 and 36<=e['bytes']<=3840,'CAP event/header classification')
  w=h['raw'];ck((w[0]&65535,w[0]>>16,w[6]&65535,w[6]>>16,w[7]&255,(w[7]>>8)&255)==(e['declared'][0],e['type'],e['declared'][1],h['inner'],e['eid'],e['seq']),'CAP raw header correlation')
  ck(positions['CAP EVENTHEADER'][j]==positions['CAP EVENT'][j]+1,'CAP paired row order')
  if j==len(ce)-1:ck(e['kind']==1 and e['eid']==0xec and e['bytes']>=40 and e['debug_type']==e['debug_bytes']==0 and r('CAP RX')['bytes']==e['bytes'],'CAP final reply')
  else:ck(e['kind']==2 and e['eid']==0x27 and e['bytes']>=48 and e['debug_type']==3 and e['debug_bytes']<=min(1200,e['bytes']-48),'CAP bounded debug')
 if ce:
  actual_sequence=ce[-1]['seq'];eq('CAP RX',echo=int(actual_sequence==r('CAP ARM')['seq']),mismatch=int(actual_sequence!=r('CAP ARM')['seq']))
 before('CAP OWNER','CAP EVENT');before('CAP EVENTHEADER','BASIC ARM')
 # Exact successful BASIC transport+actual selected metadata, independently.
 exact('BASIC ARM',(2,5,76,3000000,16));b=r('BASIC');eq('BASIC',status=0,phase=5,complete=1,skipped=0,reason=0,halt=0,error=0,submit=1,txdone=1,publications=1)
 ck(1<=b['polls']<=3000 and b['debug']==b['rearmed']<=16 and b['callbacks']>0,'BASIC bounded receipt')
 bt=r('BASIC TX');eq('BASIC TX',cid=2,seq=5,bytes=76,**{'from':4,'to':5},completed=5,didx=5,ack_expected=0,firmware_ack=0)
 ck(bt['control']&0x80000000 and bt['consumed']==int(bt['control']==0x80000000),'BASIC DDONE')
 bd=r('BASIC TXDESC');eq('BASIC TXDESC',address=0x90000040,expected=0x90005000,software_used=76);ck(1<=bd['observed']<=b['polls'] and bd['raw'][1]==bt['control'],'BASIC owned descriptor')
 br=r('BASIC RESOURCE');eq('BASIC RESOURCE',valid=1,version=0x80000001,debit=(1,0),reserved=1,remaining=2);ck(br['pse'][1]==3 and all(0<x<=65535 for x in br['pages']) and br['extra']<=255,'BASIC resource provenance')
 eq('BASIC CAPS',valid=1,sku=0,efuse=0,country=0,phy=1,band5=1,bw=2,nss=2,path=15,known=tlv['known'],unknown=tlv['unknown'])
 d=r('BASIC DEBUG');h=r('BASIC DEBUGHEADER')
 if not b['debug']:ck(not any(v for value in d.values() for v in (value if isinstance(value,tuple) else (value,))) and h['valid']==0 and not any(h['raw']),'BASIC absent debug')
 else:
  ck(d['valid']==h['valid']==1 and d['kind']==2 and d['type']<=65535 and d['type']&0x380f==0x3800 and d['eid']==0x27 and d['inner']<=65535 and d['format']==3 and d['seq']<=255 and d['id']<=65535 and 48<=d['bytes']<=3840 and d['length']<=min(1200,d['bytes']-48),'BASIC bounded debug')
  ck(d['index']==(2+cap['events']+b['debug'])%16,'BASIC debug index');w=h['raw'];ck((w[0]&65535,w[0]>>16,w[6]&65535,w[6]>>16,w[7]&255,(w[7]>>8)&255)==(d['declared'][0],d['type'],d['declared'][1],d['inner'],d['eid'],d['seq']),'BASIC debug header correlation')
 exact('BASIC OWNER',(1,0,0,0,0,0,5,0));exact('BASIC WORKER',(1,1,1));exact('CAP WORKER',(1,1,1));before('BASIC OWNER','BACKEND ARM');before('RXQUEUE','BASIC WORKER');before('BASIC WORKER','CAP WORKER')
 # Exact22 recipe and publication-credit-DDONE accounting.
 normal=r('NORMAL');eq('NORMAL',status=0,phase=7,prepared=1,halt=0,error=0,refusal=0,cursor=22,count=22,complete=22,current_cid=0)
 ck(normal['event_limit']==int(normal['events']==8192),'NORMAL event limit correlation')
 ck(normal['scan_complete'] in (0,1) and normal['service_timed_out'] in (0,1) and normal['event_limit'] in (0,1) and normal['callbacks']>0 and normal['pumps']>0,'NORMAL bounds/status')
 exact('NORMAL CREDIT',(3,1,2,2,0,22,22,0,0))
 bounds=r('NORMAL BOUNDS');ck(bounds['command_us']==3000000 and bounds['event_limit']==8192 and bounds['pump_limit']==16 and bounds['deadline']==bounds['started']+60000000 and bounds['started']<=bounds['last']<bounds['deadline'],'NORMAL time/event bounds')
 ck(bounds['command_deadline']>bounds['started'] and bounds['command_deadline']<=bounds['last']+3000000,'NORMAL command deadline')
 if bounds['service_deadline']:ck(bounds['started']<bounds['service_deadline']<bounds['deadline'] and (not normal['service_timed_out'] or bounds['last']>=bounds['service_deadline']),'NORMAL service cutoff')
 else:ck(not normal['service_timed_out'] and bounds['last']<bounds['command_deadline'],'NORMAL missing service lifetime')
 commands=rows.get('NORMAL COMMAND',[]);ck(len(commands)==22,'NORMAL command count')
 recipe=[(15,120)]*2+[(73,1536)]*3+[(72,1564)]*12+[(73,1536)]*3+[(17,76),(3,1252)]
 for j,(cmd,expected) in enumerate(zip(commands,recipe)):
  ck((cmd['n'],cmd['cid'],cmd['seq'],cmd['bytes'],cmd['from'],cmd['to'],cmd['didx'])==(j,expected[0],6+j,expected[1],5+j,6+j,6+j),'NORMAL recipe/sequence/index')
  ck(cmd['attempted']==cmd['published']==cmd['released']==cmd['txdone']==1 and 1<=cmd['observed']<=cmd['polls']<=3000 and cmd['raw'][1]&0x80000000,'NORMAL publication/DDONE')
 before('BACKEND ARM','NORMAL');before('NORMAL BOUNDS','NORMAL COMMAND');before('NORMAL COMMAND','BACKEND')
 channels=r('BACKEND')['channels'];ck(1<=channels<=39,'BACKEND channel bound');exact('BACKEND ARM',(22,channels,1,12));exact('BACKEND',(0,1,1,0,22,channels,12,0,0))
 rx=r('RX');scan=r('SCAN');eq('RX',entered=1,halt=0,error=0,phase=0);ck(rx['queue']<5 and rx['packets']==rx['rearmed']==normal['events']<=8192 and rx['malformed']<=rx['packets'] and rx['dropped']<=rx['packets'] and rx['debug']+rx['sleepy']+rx['unknown']<=rx['events']<=rx['packets'],'RX accounting')
 queues=rows.get('RXQUEUE',[]);ck(len(queues)==5,'RXQUEUE count')
 for j,q in enumerate(queues):
  size=16 if j in (1,3,4) else 1024;initial=3+cap['events']+b['debug'] if j==1 else 0
  ck(q['n']==j and q['next']==(initial+q['rearmed'])%size and q['didx']<size and q['packets']==q['rearmed'] and q['dropped']<=q['packets'] and q['segmented'] in (0,1),'RXQUEUE slot/accounting')
 ck(sum(q['packets'] for q in queues)==rx['packets'] and sum(q['rearmed'] for q in queues)==rx['rearmed'] and sum(q['dropped'] for q in queues)<=rx['dropped'],'RXQUEUE aggregate')
 events=rows.get('RXEVENT',[]);ck(len(events)==min(rx['events'],64),'RXEVENT bounded history')
 for j,e in enumerate(events):
  ck(e['n']==j and 36<=e['bytes']<=3840 and e['outer']<=65535 and e['outer']&0x380f==0x3800 and e['inner']<=65535 and e['eid']<=255 and e['seq']<=255 and e['status']<=255,'RXEVENT metadata')
  kind=e['kind'];ck(kind==3 or e['status']==0,'RXEVENT captured status');ck(kind in (1,2,3,6) and (e['eid']==13 if kind==1 else e['eid']==39 if kind==2 else e['eid']==7 if kind==3 else e['eid'] not in (13,39,7,240,253)),'RXEVENT class')
  if kind==1:ck(e['bytes']>=512,'RXEVENT completion extent')
  if kind==2:ck(e['bytes']>=48,'RXEVENT debug extent')
  if kind==3:ck(e['bytes']>=40,'RXEVENT sleepy extent')
 eq('SCAN',requested=1,sequence=1);ck(scan['done'] in (0,1) and scan['timed_out'] in (0,1) and scan['partial'] in (0,1) and scan['pending'] in (0,1) and scan['bss']<=16 and scan['bss']<=scan['frames']<=rx['packets'] and scan['dropped']<=scan['frames'] and ((scan['bss']==0)==(scan['frames']==0)) and (not scan['dropped'] or (scan['bss']==16 and scan['frames']>=16+scan['dropped'])),'SCAN bounded snapshot');ck(rx['msdu']+rx['reports']+rx['events']+scan['frames']<=rx['packets'],'RX disjoint report/frame accounting')
 ck(normal['scan_complete']==scan['done'] and (not scan['dropped'] or scan['partial']) and (not scan['timed_out'] or scan['partial']),'SCAN completion correlation')
 if scan['done']:
  ck(not scan['timed_out'] and not normal['service_timed_out'] and scan['pending']==1 and scan['drain']==1 and scan['completed_channels']<=64 and (scan['state']==7 or scan['partial']) and (any(e['kind']==1 for e in events) or (rx['events']>64 and len(events)==64)),'SCAN real completion/drain')
 else:
  ck(scan['partial']==1 and scan['drain']==0 and scan['state']==scan['completed_channels']==0 and (normal['service_timed_out'] or normal['event_limit']) and (scan['timed_out'] or normal['event_limit']) and (not normal['service_timed_out'] or scan['timed_out']),'SCAN truthful partial lifetime')
 before('BACKEND','RX');before('RX','SCAN');before('SCAN','RXQUEUE')
 if events:before('RXQUEUE','RXEVENT');before('RXEVENT','BASIC WORKER')
 expected_order=MAIN_KEYS+['CAP ARM','CAP','CAP RX','CAP DESCRIPTOR','CAP TLV','CAP OWNER']+['CAP EVENT','CAP EVENTHEADER']*len(ce)+['BASIC ARM','BASIC','BASIC TX','BASIC TXDESC','BASIC RESOURCE','BASIC CAPS','BASIC DEBUG','BASIC DEBUGHEADER','BASIC OWNER','BACKEND ARM','NORMAL','NORMAL CREDIT','NORMAL BOUNDS']+['NORMAL COMMAND']*22+['BACKEND','RX','SCAN']+['RXQUEUE']*5+['RXEVENT']*len(events)+['BASIC WORKER','CAP WORKER']
 ck(ordered==expected_order,'combined source record order');ck(not aux or max(aux)<positions['MAIN'][0],'MAIN auxiliary lifetime')
 if issues:return result
 clean=bool(scan['done'] and scan['state']==7 and not scan['partial'])
 result.update(terminal_receipt_valid=True,complete=clean,backend_config_transferred=True,scan_complete=bool(scan['done'] and scan['state']==7),scan_done_observed=bool(scan['done']),partial_cache=not clean,timed_out=bool(scan['timed_out']),bss_count=scan['bss'],retained=True,classification='backend_scan_completed' if clean else 'backend_scan_timed_out' if scan['timed_out'] else 'backend_scan_partial_cache')
 return result
if __name__=='__main__':
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('log',type=Path);a=ap.parse_args()
 with a.log.open('rb') as f:receipt=parse(f.read(LIMIT+1))
 print(json.dumps(receipt,indent=2));raise SystemExit(0 if receipt['complete'] else 2)
