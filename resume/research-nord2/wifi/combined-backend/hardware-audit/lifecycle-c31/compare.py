#!/usr/bin/env python3
"""Pinned, read-only comparison. No device access or packet payload output."""
from pathlib import Path
import hashlib,json,re
P=Path(__file__).resolve().parent
R=next(p for p in P.parents if (p/'shim/nord2-xnu').is_dir())
inputs={
 '1c5':('research/nord2/wifi-async-scan/ios/backend-first-receipt.log','bbc7ec70c6709c9c1a2c4ea910dae2ac4bc809bc2b42406394fb472b48422560',129197),
 'c31':('research/nord2/wifi-rx-lifecycle/ios/backend-first-receipt.log','465dabc7732177ad311a04b28dc3afdeaec5f4a413715c6187537967daf46dff',204038)}
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
logs={}
for name,(path,h,n) in inputs.items():
 f=R/path;assert f.stat().st_size==n and sha(f)==h;logs[name]=f.read_text()
def rows(name,key):return re.findall('^NORD2 WIFI '+re.escape(key)+': (.+)$',logs[name],re.M)
def fields(row):return dict(re.findall(r'(\w+)=([^ ]+)',row))
assert rows('1c5','RXDMAQUEUE')==rows('c31','RXDMAQUEUE')
assert rows('1c5','SCAN')==rows('c31','SCAN')
assert rows('1c5','NORMAL COMMAND')[:20]==rows('c31','NORMAL COMMAND')[:20]
results={}
for name,count in [('1c5',22),('c31',26)]:
 normal=fields(rows(name,'NORMAL')[0]);rx=fields(rows(name,'RX')[0]);rf=fields(rows(name,'SCANRF')[0]);queues=list(map(fields,rows(name,'RXQUEUE')));channels=list(map(fields,rows(name,'SCANRFCHANNEL')));dma=list(map(fields,rows(name,'RXDMA')))
 assert int(normal['complete'])==count and normal['scan_complete']=='1'
 assert len(channels)==22 and all(q['beacon_probe']=='0'for q in channels)
 assert all(q['next']==q['didx']==q['packets']==q['rearmed']=='0'for i,q in enumerate(queues)if i!=1)
 assert rx['dropped']==rx['malformed']==rx['msdu']==rx['reports']=='0'
 assert dma[0]['cfg']==dma[1]['cfg']=='0x5010b875/0x5810b875'
 results[name]={'commands':count,'all_commands_onepoll_DDONE':all(fields(q)['polls']==fields(q)['txdone']=='1'for q in rows(name,'NORMAL COMMAND')),'normal_events':int(rx['events']),'normal_sleepy':int(rx['sleepy']),'beacons':int(rf['beacons']),'MDRDY_total':sum(int(c['mdrdy'])for c in channels),'sparse_raw':rf['sparse'],'idle_raw_range':[min(int(c['idle_raw'])for c in channels),max(int(c['idle_raw'])for c in channels)],'scan_time_raw_values':sorted(set(int(c['scan_time_raw'])for c in channels)),'baseline_to_final_started_host_us':int(dma[1]['started'])-int(dma[0]['started'])}
report={'verdict':'NO_USEFUL_RX_OR_RF_IMPROVEMENT_OBSERVED','inputs':{k:{'path':p,'sha256':h,'bytes':n}for k,(p,h,n)in inputs.items()},'observations':results,'identical':['All eight RXDMAQUEUE rows byteidentical: bases/count/CIDX/DIDX/finalEXT/currentowneddescriptor','Both GLO_CFG values and allfour DMA0queue cursors/packetcounters','SCAN requested1/sequence1/done1/timed_out0/partial0/bss0/frames0/state7/completed_channels22','All first20 normalcommand rows byteidentical'], 'differences':['Fournewcommands execute: CID70x3 length348 +CID0a length132; all26 DDONEinonepoll, softwarecredits26acquired/26released andavailable2. This isnotFWapplicationACK.','Normalcontrol sleepy13→8 andeventcursor2→13; expected asynchronous variation, no data delivery','Sparsechannel1→7 andMDRDYsinglecounton165→0; nocausalRFimprovement follows','Snapshotspan1.028s→1.176s andsmallrawchannelcounterdifferences; rawidle/timeunits orRFclockhealth are not established'], 'conclusion':'CID70/default/filter lifecycle completion did not resolve the real emptyRF scan. No DMA register correction or speculative radio writes justified.','canonical_changes':False,'device_actions':False}
(P/'comparison.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({'verdict':report['verdict'],'comparison_sha256':sha(P/'comparison.json')}))
