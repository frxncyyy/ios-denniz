#!/usr/bin/env python3
"""Immutable receipt comparison. No device access or SSID/MAC/payload output."""
from pathlib import Path
import hashlib,importlib.util,json,re
P=Path(__file__).resolve().parent;R=next(x for x in P.parents if (x/'shim/nord2-xnu').is_dir());H=lambda b:hashlib.sha256(b).hexdigest()
inputs={'c31':('research/nord2/wifi-rx-lifecycle/ios/backend-first-receipt.log',204038,'465dabc7732177ad311a04b28dc3afdeaec5f4a413715c6187537967daf46dff'),'6a6a':('research/nord2/wifi-vcn33/ios/backend-first-receipt.log',217291,'10056bd1878f4b03230f7709643f973b2e336051fb173a97c112927db00d0941')}
logs={};parsed={}
for name,(path,n,h) in inputs.items():
 b=(R/path).read_bytes();assert len(b)==n and H(b)==h;logs[name]=b.decode()
 parser=R/('research/nord2/wifi-vcn33/ios/parse-backend.py' if name=='6a6a' else 'research/nord2/wifi-rx-lifecycle/ios/parse-backend.py')
 spec=importlib.util.spec_from_file_location('audit_'+name,parser);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);result=m.parse(b)
 assert result['complete'] and result['terminal_receipt_valid'] and not result['issues'];parsed[name]={'complete':result['complete'],'terminal_receipt_valid':result['terminal_receipt_valid'],'issues':result['issues'],'parser_sha256':H(parser.read_bytes())}
def rows(name,key):return re.findall(r'^NORD2 WIFI '+re.escape(key)+r': (.+)$',logs[name],re.M)
def fields(row):return dict(re.findall(r'(\w+)=([^ ]+)',row))
def one(name,key):
 a=rows(name,key);assert len(a)==1;return fields(a[0])
results={}
for name in inputs:
 normal=one(name,'NORMAL');scan=one(name,'SCAN');rx=one(name,'RX');rf=one(name,'SCANRF');channels=list(map(fields,rows(name,'SCANRFCHANNEL')));queues=list(map(fields,rows(name,'RXQUEUE')));dma=list(map(fields,rows(name,'RXDMA')))
 assert normal['complete']==normal['count']=='26' and normal['scan_complete']=='1' and normal['halt']==normal['error']=='0'
 assert scan['done']=='1' and scan['state']=='7' and scan['completed_channels']=='22' and scan['timed_out']==scan['partial']=='0'
 assert len(channels)==22 and len(queues)==5 and len(dma)==2
 assert all(q['next']==q['didx']==q['packets']==q['rearmed']=='0' for q in queues if q['n']!='1')
 assert int(rx['packets'])==int(rx['events'])+int(scan['frames']) and rx['packets']==rx['rearmed']
 assert rx['dropped']==rx['malformed']==rx['unknown']==rx['msdu']==rx['reports']=='0'
 bands={}
 for band,pred in [('2.4GHz',lambda ch:ch<=14),('5GHz',lambda ch:ch>14)]:
  c=[q for q in channels if pred(int(q['channel']))];bands[band]={'channels':len(c),'beacon_probe_total':sum(int(q['beacon_probe']) for q in c),'MDRDY_total':sum(int(q['mdrdy'])for q in c)}
 results[name]={'scan':scan,'normal_complete':int(normal['complete']),'credits':one(name,'NORMAL CREDIT'),'rx':rx,'rx_queue_summary':queues,'RF_beacons':int(rf['beacons']),'RF_bands':bands,'DMA_cfg':[d['cfg']for d in dma],'host_snapshot_span_us':int(dma[1]['started'])-int(dma[0]['started'])}
# Recipe identity excludes raw completed descriptor metadata, which is intentionally opaque.
command_keys=('n','cid','seq','bytes','from','to','didx','polls','attempted','published','released','txdone','observed')
commands={name:[{k:f[k]for k in command_keys}for f in map(fields,rows(name,'NORMAL COMMAND'))]for name in inputs}
assert commands['c31']==commands['6a6a'] and len(commands['6a6a'])==26
assert rows('c31','RXDMAQUEUE')==rows('6a6a','RXDMAQUEUE')
assert results['c31']['DMA_cfg']==results['6a6a']['DMA_cfg']
assert results['c31']['credits']==results['6a6a']['credits']
vcns=[]
for row in rows('6a6a','VCN33 SNAP'):
 f=fields(row);v=[int(x,0)for x in f['values'].split('/')];assert len(v)==13 and int(f['valid'],0)==0x1fff
 vcns.append({'kind':int(f['kind']),'phase':int(f['phase']),'time_us':int(f['time_us']),'MON':[v[1],v[7]],'OP_EN':[v[2],v[8]],'VOSEL':[v[5],v[11]],'SW_EN0':[v[0]&1,v[6]&1],'SW_EN1':[(v[4]>>15)&1,(v[10]>>15)&1],'VS2_raw':v[12]})
assert [q['kind']for q in vcns[:4]]==[0,1,2,3]
assert vcns[0]['MON']==vcns[1]['MON']==[0,0]
assert all(q['MON']==[3,3]for q in vcns[2:])
assert all(q['VOSEL']==[0xd00,0xd00] and q['OP_EN']==[0x8060,0x8040]for q in vcns[1:])
assert all(q['SW_EN0']==q['SW_EN1']==[0,0]and q['VS2_raw']==0x30 for q in vcns)
periodic=[q for q in vcns if q['kind']==4];phases={p:sum(q['phase']==p for q in periodic)for p in sorted(set(q['phase']for q in periodic))}
report={'result':'PASS_ACTUAL_VCN33_CONFIG_AND_FIRST_RF_RX_BSS_OBSERVED','inputs':{k:{'path':path,'bytes':n,'sha256':h}for k,(path,n,h)in inputs.items()},'independent_strict_parses':parsed,'comparison':results,'unchanged':['All26command CID/seq/extent/ring/DIDX/onepoll/publication/DDONE/software-release fields; opaque raw completion words not used as identity','ActualPSE3/BASICspent1/capacity2/available2/acquired26/released26/PLE0/FWACK0','All8DMA0 RXDMAQUEUE readback rows byte-identical; both GLO_CFG values unchanged'],'rail_observations':{'status':one('6a6a','VCN33'),'sync':vcns[:4],'periodic_count':len(periodic),'periodic_phase_counts':phases,'periodic_time_range_us':[periodic[0]['time_us'],periodic[-1]['time_us']],'all_periodic_MON':[3,3],'all_valid':True,'VS2_raw':0x30},'causal_assessment':'Strong source-backed intervention evidence: adding guarded VCN33 callback plus observations coincides with first received RF frames/BSS and positive2.4GHz counters, while26recipe/DMAreadbacks remain unchanged. One trial does not isolate selector-versus-RC contribution or timing/environment confounds, and oldc31 has no laterVCNmeasurements. Do not convert earlyc31MON0 into a whole-boot trace.','limits':['7BSS records from8management scanframes; not seven established connections','5GHz ninechannels havezero beacon_probe/MDRDY inthisscan; notproof5GHzunsupported nor permissiontotuneVS2/RF','All21normalRXpackets arrive eventring1:13control(12sleepy+done)+8managementframes; DMA0data/report queuesremainempty, msdu0, notassociateddataRX','MON3 means sampledB_EN/B_STB; no continuousstate/voltageADC/RFcalibration proof','VS2@18b4 raw30 read-only unchangedwithinthisreceipt; c31 valuewasnotcaptured, no crossbootdifferenceclaim','All26DDONE/creditreturns remain host/softwarecompletion, firmware_ack0','Nativeinterface0/association0; no iOS IOService/interface/association/IP/network readiness established','Firstimmutable snapshotonly; rootcollects180s separately, no secondUSBreader/deviceaction']}
(P/'comparison.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'result':report['result'],'comparison_sha256':H((P/'comparison.json').read_bytes()),'periodic_samples':len(periodic),'periodic_phases':phases,'RF_bands':results['6a6a']['RF_bands'],'RXpacket_split':[int(results['6a6a']['rx']['events']),int(results['6a6a']['scan']['frames'])]}))
