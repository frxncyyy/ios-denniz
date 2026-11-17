#!/usr/bin/env python3
"""Private finite reader regression; hardware input is read only, never copied."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,re
P=Path(__file__).resolve().parent

def load(name):
 s=importlib.util.spec_from_file_location(name.replace('-','_'),P/(name+'.py'));m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
backend=load('parse-backend');ios=load('parse-ios-service')
positive=negative=0

def check(data,good,service=False):
 global positive,negative
 r=ios.parse(data) if service else backend.parse(data.encode())
 actual=not r['issues'] and (bool(r['successful_fresh_scan_completions']) if service else r['complete'])
 assert actual==good,(good,r['issues'],r.get('classification'))
 if good:positive+=1
 else:negative+=1
 return r

def row(kind,**kw):return 'NORD2 WIFI IOS '+kind+': '+' '.join(str(k)+'='+str(v) for k,v in kw.items())+'\n'

def service(ticket=1,gen=1,source=3,display=5,retained=6,overflow=False):
 return (row('ENQUEUE',status=0,generation=gen,ticket=ticket,t_ms=100)+
 row('SCAN',ticket=ticket,generation=gen,status=int(overflow),sequence=1,frames=65 if overflow else source,bss=source,done=1,partial=int(overflow),canceled=0,error=0,rx_error=0)+
 row('SCANHISTORY',generation=gen,source_rows=source,display_rows=display,retained_rows=retained,overflow=int(overflow),dropped=int(overflow),ttl_ms=30000)+
 row('COMPLETE',generation=gen,ticket=ticket,backend=int(overflow),current=1,cache_status=0,rows=display,omitted=0,omit_mask=0,t_ms=150)+
 row('RESULT',status=int(bool(display)),next=int(bool(display)),count=display,generation=gen))

def mutate_line(text,key,fn):
 return ''.join(fn(l) if 'NORD2 WIFI '+key+':' in l else l for l in text.splitlines(keepends=True))
def field(l,k,v):return re.sub(r'(?<![a-z_])'+k+r'=[^ \n]+',k+'='+str(v),l)

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--baseline',type=Path,required=True);ap.add_argument('--report',type=Path,required=True);a=ap.parse_args()
 raw=a.baseline.read_bytes();old=raw.decode();assert hashlib.sha256(raw).hexdigest()=='16ed6261479b4a3e861fe164245247ace3ac5b09a58970a310e2cdf99f6ce1ea'
 check(old,False)
 text=mutate_line(old,'BACKEND ARM',lambda l:field(l,'channels',37));text=mutate_line(text,'BACKEND',lambda l:field(l,'channels',37));text=mutate_line(text,'SCAN',lambda l:field(l,'completed_channels',37))
 channels=list(range(1,14))+[36,40,44,48,52,56,60,64]+list(range(100,141,4))+[149,153,157,161,165]
 assert len(channels)==37
 lines=text.splitlines(keepends=True);start=next(i for i,l in enumerate(lines) if 'NORD2 WIFI SCANRFCHANNEL:' in l);oldrows={int(re.search(r' channel=(\d+)',l)[1]):l for l in lines if 'NORD2 WIFI SCANRFCHANNEL:' in l}
 lines=[l for l in lines if 'NORD2 WIFI SCANRFCHANNEL:' not in l]
 generated=[]
 for j,ch in enumerate(channels):
  l=oldrows.get(ch,f'NORD2 WIFI SCANRFCHANNEL: n=0 channel={ch} beacon_probe=0 mdrdy=0 idle_raw=0 scan_time_raw=0\n');generated.append(field(l,'n',j))
 lines[start:start]=generated;text=''.join(lines);text=mutate_line(text,'SCANRF',lambda l:field(l,'channels',37));check(text,True)
 # Synthetic cache-overflow accounting uses queue0 and its software cursor.
 overflow=mutate_line(text,'SCAN',lambda l:field(field(field(field(l,'bss',64),'frames',65),'dropped',1),'partial',1))
 overflow=mutate_line(overflow,'NORMAL',lambda l:field(l,'events',79));overflow=mutate_line(overflow,'RX',lambda l:field(field(l,'packets',79),'rearmed',79))
 overflow=mutate_line(overflow,'RXQUEUE',lambda l:field(field(field(field(l,'next',57),'didx',57),'packets',57),'rearmed',57) if 'n=0 ' in l else l)
 overflow=mutate_line(overflow,'RXDMAQUEUE',lambda l:field(field(l,'next',57),'descriptor',hex(0x9002c000+57*16)) if 'n=1 slot=0 ' in l else l)
 r=check(overflow,True);assert r['overflow_cache'] and r['partial_cache']
 for key,value in [('bss',65),('bss',63),('dropped',2),('partial',0),('timed_out',1),('state',6)]:check(mutate_line(overflow,'SCAN',lambda l,k=key,v=value:field(l,k,v)),False)
 check(mutate_line(text,'BACKEND',lambda l:field(l,'channels',22)),False)
 for n in [0,1,16,64]:
  good=service(source=n,display=n,retained=n);check(good,True,True)
 check(service(ticket=2**64-1,gen=2**64-1),True,True)
 good=service();check(good,True,True);check(good+service(2,2,1,6,6),True,True)
 over=service(source=64,display=64,retained=64,overflow=True);check(over,True,True)
 # Complete exact source/history pairs; unknown fields cannot become evidence.
 for needle,replacement in [('ttl_ms=30000','ttl_ms=30001'),('source_rows=3','source_rows=4'),('display_rows=5','display_rows=65'),('retained_rows=6','retained_rows=4'),('overflow=0','overflow=1'),('dropped=0','dropped=1'),('backend=0','backend=1'),('done=1','done=0'),('frames=3','frames=2'),('sequence=1','sequence=0'),('bss=3','bss=65'),('generation=1','generation=0'),('canceled=0','canceled=1'),('error=0','error=1'),('cache_status=0','cache_status=0 secret=1'),('rows=5','rows=4'),('generation=1 source_rows','generation=2 source_rows')]:check(good.replace(needle,replacement,1),False,True)
 hist=next(l for l in good.splitlines(keepends=True) if 'SCANHISTORY:' in l)
 check(good.replace(hist,''),False,True);check(good.replace(hist,hist+hist),False,True);check(hist+good,False,True)
 check(over.replace('frames=65','frames=64'),False,True);check(over.replace('bss=64','bss=63'),False,True)
 # Truncation, wrong completion identity and unrelated scan interleaving refuse.
 check(good.replace(hist,hist.rstrip('\n')),False,True);check(good.replace('ticket=1 backend','ticket=2 backend'),False,True)
 scan2=service(2,2).splitlines(keepends=True)[1];check(good.replace(hist,hist+scan2),False,True)
 for k in ['SCANHISTORY','COMPLETE','SCAN','ENQUEUE','RESULT']:
  check(good.replace('NORD2 WIFI IOS '+k+': ','NORD2 WIFI IOS '+k+': unexpected=1 ',1),False,True)
 a.report.write_text(json.dumps(dict(result='PASS',positive=positive,negative=negative,baseline_sha256=hashlib.sha256(raw).hexdigest(),synthetic=True,hardware_claim=False),indent=2)+'\n')
 print(json.dumps(dict(result='PASS',positive=positive,negative=negative)))
if __name__=='__main__':main()
