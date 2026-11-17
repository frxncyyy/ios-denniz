#!/usr/bin/env python3
"""Strict new captured metadata layered on unchanged v3 core scan proof."""
import importlib.util,hashlib,json,argparse
from pathlib import Path
P=Path(__file__).resolve().parent
b=P/'core-async.py'
if hashlib.sha256(b.read_bytes()).hexdigest()!='0cef01dc160bcbe913ab40bf61b356ab9c89a8685a7fbc07edc52a244cd9acfe':raise RuntimeError('async core identity')
s=importlib.util.spec_from_file_location('basev3',b);base=importlib.util.module_from_spec(s);s.loader.exec_module(base)
SCHEMA={'RXDMA':'n attempted complete error reason valid cfg/2 started last','RXDMAQUEUE':'n slot q next valid base expected control/4 ext_addr ext descriptor raw/4','SCANRF':'version pno beacons split/2 sparse/3 channels','SCANRFCHANNEL':'n channel beacon_probe mdrdy idle_raw scan_time_raw'}
def parse(data):
 issues=[];clean=[];observed=[];old=[];rows={k:[] for k in SCHEMA}
 if len(data)>base.LIMIT:return base.parse(data)
 try:lines=data.decode('utf8').splitlines(keepends=True)
 except UnicodeDecodeError:return base.parse(data)
 for line in lines:
  at=line.find('NORD2 WIFI ');body=line[at:].rstrip('\r\n') if at>=0 else ''
  key=body[11:].split(':',1)[0] if body else ''
  if key in SCHEMA:
   try:
    if not line.endswith('\n') or len(body)>511 or line.count('NORD2 WIFI ')!=1:raise ValueError('truncated/interleaved')
    row=base.decode(body,key,SCHEMA[key])
    # Expected/descriptor are physical uint64; selected current pool is low32.
    rows[key].append(row);observed.append(key)
   except ValueError:issues.append('malformed '+key)
  else:
   clean.append(line)
   if key:old.append(key)
   if key in ('RXQUEUE','RXEVENT','BASIC WORKER') and observed:observed.append('@'+key)
 result=base.parse(''.join(clean).encode())
 def ck(v,why):
  if not v and len(issues)<64:issues.append(why)
 def oldrow(key):
  for l in clean:
   at=l.find('NORD2 WIFI '+key+': ')
   if at>=0:return base.decode(l[at:].rstrip('\r\n'),key,base.SCHEMA[key])
  return {}
 if result['terminal_receipt_valid']:
  scan=oldrow('SCAN');normal=oldrow('NORMAL');bounds=oldrow('NORMAL BOUNDS')
  final_queues=[base.decode(l[l.find('NORD2 WIFI RXQUEUE: '):].rstrip('\r\n'),'RXQUEUE',base.SCHEMA['RXQUEUE']) for l in clean if 'NORD2 WIFI RXQUEUE: ' in l]
  ck(len(rows['RXDMA'])==2 and len(rows['RXDMAQUEUE'])==8,'snapshot exact counts')
  if len(rows['RXDMA'])==2 and len(rows['RXDMAQUEUE'])==8:
   for n,d in enumerate(rows['RXDMA']):
    ck(d['n']==n,'snapshot ordinal')
    active=n==0 or scan['done'] or scan['timed_out']
    if not active:
     ck(all(v==0 or v==(0,0) for k,v in d.items() if k!='n'),'unused final snapshot zeros')
    else:
     ck(d['attempted']==d['complete']==1 and d['error']==0 and d['valid']==3 and d['reason']==(1 if n==0 else 2 if scan['done'] else 3),'snapshot receipt masks/reason')
     ck(bounds['started']<=d['started']<=d['last']<=bounds['last'],'snapshot bounded time')
    for j in range(4):
     q=rows['RXDMAQUEUE'][4*n+j]
     ck(q['n']==n and q['slot']==j,'snapshot queue ordinal')
     if not active:
      ck(all(v==0 or v==(0,0,0,0) for k,v in q.items() if k not in ('n','slot')),'unused queue zeros');continue
     qi=[0,2,3,4][j];count=[1024,1024,16,16][j];desc=[0x9002c000,0x90030000,0x90034000,0x90034100][j]
     ck(q['q']==qi and q['next']<count and q['valid']==63,'queue identity/bounds/valid')
     if n==1 and scan['done']:ck(q['next']==final_queues[qi]['next'],'clean done final queue cursor correlation')
     ck(q['base']==0x18024500+16*j and q['expected']==desc and q['descriptor']==desc+16*q['next'] and q['ext_addr']==0x18024680+4*j,'queue software address correlation')
   ck(rows['RXDMA'][0]['last']<=rows['RXDMA'][1]['started'] or not rows['RXDMA'][1]['attempted'],'snapshot chronological')
  rf=rows['SCANRF'];channels=rows['SCANRFCHANNEL'];ck(len(rf)==int(bool(scan['done'])),'RF only with actual SCAN_DONE')
  if rf:
   d=rf[0];ck(d['version']<=255 and d['pno']<=255 and all(x<=255 for x in d['sparse']) and d['channels']<=64 and len(channels)==d['channels'],'RF widths/count')
   for j,q in enumerate(channels):ck(q['n']==j and q['channel']<=255 and q['beacon_probe']<=255 and q['mdrdy']<=255 and q['idle_raw']<=65535 and q['scan_time_raw']<=65535,'RF channel ordinal/width')
  else:ck(not channels,'RF orphan channels')
  # New diagnostics must occupy exact emitter slots, never hide unknown tags.
  pattern=['RXDMA']+['RXDMAQUEUE']*4+['RXDMA']+['RXDMAQUEUE']*4
  events=sum(1 for x in old if x=='RXEVENT')
  pattern+=['@RXEVENT']*events+(['SCANRF']+['SCANRFCHANNEL']*len(channels) if scan['done'] else [])+['@BASIC WORKER']
  ck(observed==pattern,'diagnostic exact source order')
 if issues:
  result.update(complete=False,terminal_receipt_valid=False,backend_config_transferred=False,scan_complete=False,classification='incomplete_or_conflicting')
  result['issues']=(result['issues']+issues)[:64]
 result['sha256']=hashlib.sha256(data).hexdigest();result['rx_readback_valid']=result['terminal_receipt_valid'];result['rf_counters_observed']=bool(result['terminal_receipt_valid'] and rows['SCANRF'])
 if result['rf_counters_observed']:
  rf=rows['SCANRF'][0];result['rf_metadata']={'version':rf['version'],'beacons_raw':rf['beacons'],'split_raw':rf['split'],'channels':rf['channels'],'channel_counters_raw':rows['SCANRFCHANNEL']}
 return result
if __name__=='__main__':
 a=argparse.ArgumentParser(description=__doc__);a.add_argument('log',type=Path);x=a.parse_args()
 with x.log.open('rb') as f:r=parse(f.read(base.LIMIT+1))
 print(json.dumps(r,indent=2));raise SystemExit(0 if r['complete'] else 2)
