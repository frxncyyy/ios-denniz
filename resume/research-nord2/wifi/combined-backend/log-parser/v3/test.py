#!/usr/bin/env python3
import pathlib,importlib.util,re,json,hashlib
P=pathlib.Path(__file__).resolve().parent;R=next(x for x in P.parents if x.name=='ios-sweet')
spec=importlib.util.spec_from_file_location('backend',P/'parse-backend.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
actual=R/'research/nord2/wifi-basic-config/ios/basic-failure-first.log'
assert hashlib.sha256(actual.read_bytes()).hexdigest()=='b94b01cbd0b3f03110f7811b3b5996cc2b562279448762392dfba01aee378084'
main=''.join(line+'\n' for line in actual.read_text().splitlines() if line.startswith('NORD2 WIFI MAIN') and not line.startswith('NORD2 WIFI MAIN DRAIN'))
full=P.parent.parent/'full-tests/test.log';data=full.read_text();blocks=re.findall(r'NORD2 WIFI CAP ARM:.*?NORD2 WIFI CAP WORKER:[^\n]*\n',data,re.S)
assert blocks
base=(main+blocks[0]).encode();(P/'synthetic-combined-success.log').write_bytes(base)
pos=neg=0

def accept(data,complete=True):
 global pos
 r=m.parse(data)
 assert r['terminal_receipt_valid'] and r['complete']==complete,r
 assert not any(r[k] for k in ('firmware_ack','interface_available','association_available','network_ready')),r
 pos+=1;return r

def reject(data):
 global neg
 r=m.parse(data);assert not r['terminal_receipt_valid'] and not r['complete'] and not r['backend_config_transferred'],r;neg+=1

def change(data,key,field,value,which=0):
 lines=data.decode().splitlines(True);inds=[i for i,l in enumerate(lines) if l.startswith('NORD2 WIFI '+key+':')];assert inds,(key,field)
 i=inds[which];lines[i],n=re.subn(r'(?<!\w)'+re.escape(field)+r'=[^ \r\n]+',field+'='+str(value),lines[i],count=1);assert n
 return ''.join(lines).encode()
accept(base)
for raw in ['0/0x80000000/0/0','0xffffffff/0xffffffff/0xffffffff/0xffffffff','0x12345678/0xc0000000/0x89abcdef/0xeeeeeeee']:
 b=base
 for j in range(22):b=change(b,'NORMAL COMMAND','raw',raw,j)
 accept(b)
for inner in [0,0xe000,0xffff]:accept(change(base,'RXEVENT','inner',inner))
# BASIC completed writeback otherfields are opaque; diagnosticconsumed correlates.
opaque=change(change(change(base,'BASIC TX','control','0xffffffff'),'BASIC TX','consumed',0),'BASIC TXDESC','raw','0xffffffff/0xffffffff/0xffffffff/0xffffffff');accept(opaque)
# Updated source debug inner16bit is metadata, never forcedE000. CAPdebug
# stillrequires actualformat3 +boundedbody, and everyheader mustcorrelate.
debug=base
for key,field,value,which in [('BASIC','debug',1,0),('BASIC','rearmed',1,0),('BASIC DEBUG','valid',1,0),('BASIC DEBUG','index',4,0),('BASIC DEBUG','kind',2,0),('BASIC DEBUG','type','0x3800',0),('BASIC DEBUG','eid','0x27',0),('BASIC DEBUG','seq',255,0),('BASIC DEBUG','bytes',48,0),('BASIC DEBUG','inner','0x1234',0),('BASIC DEBUG','format',3,0),('BASIC DEBUGHEADER','valid',1,0),('BASIC DEBUGHEADER','raw','0x38000000/0/0/0/0/0/0x12340000/0xff27/0',0),('RXQUEUE','next',6,1),('RXQUEUE','didx',6,1)]:debug=change(debug,key,field,value,which)
accept(debug)
reject(change(debug,'BASIC DEBUG','format',4));reject(change(debug,'BASIC DEBUGHEADER','raw','0/0/0/0/0/0/0/0/0'))
capdebug=change(change(base,'CAP EVENT','n',1),'CAP EVENTHEADER','n',1)
capdebug=capdebug.replace(b'NORD2 WIFI CAP EVENT: n=1',b'NORD2 WIFI CAP EVENT: n=0 kind=2 type=0x3800 eid=0x27 seq=255 bytes=48 declared=0/0 debug_type=3 debug_bytes=0\nNORD2 WIFI CAP EVENTHEADER: n=0 valid=1 bytes=36 inner=0x1234 raw=0x38000000/0/0/0/0/0/0x12340000/0xff27/0\nNORD2 WIFI CAP EVENT: n=1',1)
for key,field,value,which in [('CAP','events',2,0),('CAP','rearmed',2,0),('CAP','polls',3,0),('CAP RX','debug',1,0),('CAP DESCRIPTOR','raw','0x9000a000/0xf000000/0/0',0),('RXQUEUE','next',6,1),('RXQUEUE','didx',6,1)]:capdebug=change(capdebug,key,field,value,which)
accept(capdebug);reject(change(capdebug,'CAP EVENT','debug_type',4));reject(change(capdebug,'CAP EVENTHEADER','inner','0xe000'))
# CAP source accepts an actual reply sequence differingfrom outgoingquery4.
# Rawheader and event capture mustagree; echo/mismatch exactlydescribe comparison.
for actual_seq in [0,1,5,255]:
 mismatch=change(change(change(base,'CAP EVENT','seq',actual_seq),'CAP RX','echo',0),'CAP RX','mismatch',1)
 lines=mismatch.decode().splitlines(True)
 for i,line in enumerate(lines):
  if line.startswith('NORD2 WIFI CAP EVENTHEADER:'):
   words=re.search(r'raw=([^\n]+)',line)[1].split('/');word=int(words[7],0);words[7]=hex((word&~0xff00)|(actual_seq<<8));lines[i]=re.sub(r'raw=[^\n]+','raw='+'/'.join(words),line)
 mismatch=''.join(lines).encode();accept(mismatch)
 reject(change(mismatch,'CAP RX','echo',1));reject(change(mismatch,'CAP RX','mismatch',0));reject(change(mismatch,'CAP EVENT','seq',4));reject(change(mismatch,'CAP EVENTHEADER','raw','0x38000000/0/0/0/0/0/0x38000000/0x4ec/0x8a00'))
# Actual owner fixture cases2..4 contain truthful retained partial/timeouts.
for block in blocks[1:4]:
 r=accept((main+block).encode(),False);assert r['partial_cache'] and r['timed_out'] and not r['scan_complete']
# Empty snapshot is valid and never fabricates a network.
empty=change(change(change(base,'SCAN','bss',0),'SCAN','frames',0),'RX','malformed',2);accept(empty)
# Genuine completed scan with overflowing copied cache remains partial.
overflow=base
for key,field,value in [('SCAN','bss',16),('SCAN','frames',17),('SCAN','dropped',1),('SCAN','partial',1),('RX','packets',20),('RX','rearmed',20),('NORMAL','events',20),('RXQUEUE','packets',16),('RXQUEUE','rearmed',16),('RXQUEUE','next',16),('RXQUEUE','didx',16)]:overflow=change(overflow,key,field,value)
accept(overflow,False)
# Source termination/report accounting: no counterinflation or fabricated
# eventlimit/timeout terminalreasons after the complete22-command batch.
reject(change(base,'RX','msdu',6));reject(change(base,'RX','reports',6));reject(change(base,'NORMAL','event_limit',1))
partial=(main+blocks[1]).encode();reject(change(partial,'NORMAL','service_timed_out',0))
reject(change(change(base,'RX','msdu',3),'RX','reports',3))
# Source first64 history cantruncate before a later realSCAN_DONE. The
# matched-body pending/drain summaries remaintruthful; noecho is invented.
late=base
for key,field,value,which in [('NORMAL','events',69,0),('RX','packets',69,0),('RX','rearmed',69,0),('RX','events',65,0),('RX','debug',64,0),('RXQUEUE','packets',65,1),('RXQUEUE','rearmed',65,1)]:late=change(late,key,field,value,which)
history=''.join('NORD2 WIFI RXEVENT: n=%d bytes=48 outer=0x3800 inner=0x1234 eid=0x27 seq=0 kind=2 status=0\n'%i for i in range(64))
late=re.sub(rb'NORD2 WIFI RXEVENT:[^\n]*\n',history.encode(),late,count=1);accept(late)
missinglast=re.sub(rb'NORD2 WIFI RXEVENT: n=63[^\n]*\n',b'',late);reject(missinglast)
within=late
for key,field,value,which in [('NORMAL','events',68,0),('RX','packets',68,0),('RX','rearmed',68,0),('RX','events',64,0),('RXQUEUE','packets',64,1),('RXQUEUE','rearmed',64,1),('RXQUEUE','next',4,1),('RXQUEUE','didx',4,1)]:within=change(within,key,field,value,which)
assert 'SCAN real completion/drain' in m.parse(within)['issues'];reject(within)
# Direct realnativeowner regression: only fakefirmware CAPreplyseq changed
# to5; outgoing4/BASIC5/normal6..27 remain production source-built.
native5=(P/'native-seq5-first.log').read_bytes();assert hashlib.sha256((P/'native-seq5-full.log').read_bytes()).hexdigest()=='d276e9de238126bc2ca9ceaa2836bf41091ca19e0ff0c2041f981478353c7f7e'
accept(main.encode()+native5)
reject(change(main.encode()+native5,'CAP RX','echo',1));reject(change(main.encode()+native5,'CAP RX','mismatch',0));reject(change(main.encode()+native5,'CAP EVENT','seq',4))
# Completed matchedscan requires whole5queue no-progress drain; captured
# DIDX must equalnext, while pendingDDONE timeout remains valid.
for queue in range(5):
 bad=change(base,'RXQUEUE','didx',2 if queue!=1 else 6,queue);reject(bad)
reject(change(base,'NORMAL','pumps',1))
reject(change(late,'NORMAL','pumps',4)) #69events exceed4*16 budget
reject(change(base,'NORMAL COMMAND','polls',2)) #sum polls23 >pumps22
# Exhausted eventbudget legitimatelystops increasingpump_calls onlaterpolls.
# 21 earlierempty pumps +512full16-frame pumps atfinalCID3; remaining88polls
# after exhaustion mean621totalpolls exceed533pumps withoutcontradiction.
budgeted=(main+blocks[1]).encode()
for key,field,value,which in [('NORMAL','events',8192,0),('NORMAL','pumps',533,0),('NORMAL','callbacks',100000,0),('NORMAL','event_limit',1,0),('NORMAL','service_timed_out',0,0),('NORMAL BOUNDS','last',700000,0),('NORMAL BOUNDS','command_deadline',3000000,0),('NORMAL BOUNDS','service_deadline',15700000,0),('NORMAL COMMAND','polls',600,21),('RX','packets',8192,0),('RX','rearmed',8192,0),('RX','events',8188,0),('RX','debug',8188,0),('SCAN','timed_out',0,0),('RXQUEUE','packets',8188,1),('RXQUEUE','rearmed',8188,1),('RXQUEUE','next',0,1),('RXQUEUE','didx',0,1)]:budgeted=change(budgeted,key,field,value,which)
budgeted=budgeted.replace(b'NORD2 WIFI BASIC WORKER:',history.encode()+b'NORD2 WIFI BASIC WORKER:',1)
accept(budgeted,False);reject(change(budgeted,'NORMAL','pumps',511))
# SourceAXI cost1 doesnotuse page sizes asadmission prerequisites.
for pages in ['0/0','128/0','0/256']:accept(change(base,'BASIC RESOURCE','pages',pages))
reject(change(base,'BASIC RESOURCE','pages','65536/0'))
# Require every singleton record and allrepeated rows, strictno duplicates.
for line in base.splitlines(True):
 if line.startswith(b'NORD2 WIFI ') and not any(line.startswith(('NORD2 WIFI MAIN '+k+':').encode()) for k in m.MAIN_AUX):
  reject(base.replace(line,b'',1));reject(base+line)
for key,field,value in [
 ('MAIN','ready',0),('MAIN NATIVE','clock',0),('MAIN DMASTATUS','raw','0/0x20000000'),('CAP RX','irq','0/0x20000000'),('CAP OWNER','dmairq','0/0x20000000'),('CAP TLV','product',65536),('RXEVENT','status',255),('MAIN OWNER','driver',0),('MAIN GUARD','reason',1),('MAIN NATIVE','gic',1),('MAIN INIT','halt',1),('MAIN IRQ','valid','3/1'),('MAIN TRANSPORT','indices','3/4'),('MAIN TXWB','ddone',0),('MAIN SPANS','completed',126),('MAIN SPANOBS','ready',0),('MAIN PHY','backup',0),('MAIN PHYBACKUP','copied',0),
 ('CAP','complete',0),('CAP RX','echo',0),('CAP OWNER','parked',1),('CAP EVENT','seq',5),('CAP EVENT','kind',2),('CAP EVENTHEADER','valid',0),('CAP EVENTHEADER','raw','0/0/0/0/0/0/0/0/0'),('CAP TLV','resources',0),
 ('BASIC','complete',0),('BASIC TX','firmware_ack',1),('BASIC TX','control',0),('BASIC TXDESC','address','0x90000050'),('BASIC RESOURCE','remaining',3),('BASIC RESOURCE','pse','17/4'),('BASIC CAPS','sku',1),('BASIC OWNER','completed',27),
 ('BACKEND ARM','passive',0),('BACKEND','interface',1),('BACKEND','association',1),('BACKEND','nvram',11),('NORMAL','count',21),('NORMAL','halt',1),('NORMAL CREDIT','basic_spent',0),('NORMAL CREDIT','released',21),('NORMAL CREDIT','firmware_ack',1),('NORMAL BOUNDS','deadline',999999999),('NORMAL BOUNDS','pump_limit',17),
 ('RX','rearmed',4),('RX','halt',1),('RX','packets',6),('SCAN','sequence',2),('SCAN','done',0),('SCAN','timed_out',1),('SCAN','state',6),('SCAN','bss',17),('SCAN','drain',0),('RXEVENT','kind',4),('RXEVENT','outer','0x3801'),('RXEVENT','eid',240),('RXEVENT','bytes',511),
 ]:reject(change(base,key,field,value))
for j in range(22):
 for field,val in [('n',22),('cid',255),('seq',0),('bytes',4097),('from',100),('to',100),('didx',100),('polls',0),('attempted',0),('published',0),('released',0),('txdone',0),('observed',0),('raw','0/0x7fffffff/0/0')]:reject(change(base,'NORMAL COMMAND',field,val,j))
for j in range(5):
 for field,val in [('n',5),('next',1024),('didx',1024),('rearmed',0),('segmented',2)]:reject(change(base,'RXQUEUE',field,val,j))
for payload in [base[:-1],base+b'NORD2 WIFI RX BOGUS: x=0\n',base+b'NORD2 WIFI NORMAL: status=0\n',base+b'NORD2 WIFI MAIN DRAIN: sleepable=1 retained_halt=1 ms=30000; panic pending\n',base+b'\xff',base+b'NORD2 WIFI SCAN: x='+b'9'*1000+b'\n',base.replace(b'raw=0xffffffff',b'raw=0x100000000',1),base+base,base.replace(b'phase=7',b'phase=07 ',1)]:reject(payload)
lines=base.splitlines(True)
i=next(i for i,l in enumerate(lines) if l.startswith(b'NORD2 WIFI NORMAL CREDIT:'));lines[i],lines[i+1]=lines[i+1],lines[i];reject(b''.join(lines))
# Fixed schema prevents duplicate extra keys, reordered fields and lexical overflow.
for old,new in [(b'commands=22 channels=',b'commands=22 extra=0 channels='),(b'cid=0xf seq=6',b'seq=6 cid=0xf'),(b'status=0 phase=7',b'status=4294967296 phase=7'),(b'packets=5 rearmed=5',b'packets=-1 rearmed=5')]:reject(base.replace(old,new,1))
report={'status':'PASS_SYNTHETIC_ONLY','positive':pos,'negative':neg,'full_fixture_log_sha256':hashlib.sha256(full.read_bytes()).hexdigest(),'direct_native_cap_seq5_log_sha256':hashlib.sha256((P/'native-seq5-full.log').read_bytes()).hexdigest(),'main_seed_actual_log_sha256':hashlib.sha256(actual.read_bytes()).hexdigest(),'fixture_description':'Actualsaved MAIN criticalrows +synthetic actualnativeownerfixture CAP/BASIC/22commands/RX/SCAN records; neverhardwarebackendreceipt','partial_semantics':'terminal_receipt_valid true/configTXtrue; completefalse/scancompletefalse ontimeout','privacy':'parserreturnsmetadata status/counts only; noinputrows/rawbytes/MAC/SSID/BSSID/calibrationvalues'}
(P/'test-report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
