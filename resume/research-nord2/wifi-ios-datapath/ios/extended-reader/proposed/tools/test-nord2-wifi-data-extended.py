#!/usr/bin/env python3
"""Portable actual-emitter supplemental-reader tests, no hardware/private data."""
from pathlib import Path
import argparse,hashlib,json,re,runpy
P=Path(__file__).resolve().parent;R=next((p for p in P.parents if (p/'AGENTS.md').exists()),None)
a=argparse.ArgumentParser();a.add_argument('--emitter-source',type=Path);a.add_argument('--log-dir',type=Path);args=a.parse_args()
m=runpy.run_path(str(P/'nord2-wifi-data-extended.py'));parse=m['parse'];F=P/'fixtures/nord2-wifi-data-extended';positive=negative=0
FALSE=('absence_proves_zero_events','capture_integrity_proven','rx_is_bsd_delivery','dhcp_is_lease_proof','dhcp_is_authenticated','internet_proven','air_delivery_proven','last_publication_is_packet_correlation','identities_or_xids_exposed')
def good(text):
 global positive
 result=parse(text);assert result['valid'] and all(result[k] is False for k in FALSE);assert result['shared_pid127_ambiguous'] and result['matched_tuple_is_only_bounded_observation'];positive+=1;return result
def bad(text):
 global negative
 try:parse(text)
 except ValueError as e:assert 'FAKESECRET' not in str(e);negative+=1
 else:raise AssertionError('accepted contradictory/malformed supplemental record')
def row(kind,**x):return 'NORD2 WIFI '+kind+': '+' '.join(k+'='+str(x[k]) for k in m['SCHEMAS'][kind].split())+'\n'
def change(line,key,value):return re.sub(r'\b'+key+r'=[0-9]+',key+'='+str(value),line)
for name in ('actual','extended','rx'):
 text=(F/(name+'.log')).read_text();result=good(text);assert result['observed_records']
 lines=text.splitlines(keepends=True)
 for i,line in enumerate(lines):
  good(line);good(''.join(lines[:i+1]));bad(line.rstrip('\n'))
  for key,value in re.findall(r'([a-z_]+)=([0-9]+)',line):bad(change(line,key,1<<(64 if key=='g' else 32)))
  for private in ('ssid','mac','key','nonce','mic','replay_bytes','xid','client','payload'):
   bad(line.rstrip('\n')+' '+private+'=FAKESECRET\n')
  bad(line.replace(': ',': g=1 ',1));bad(line.rstrip('\n')+' trailing\n')
 good(text.replace('\n','\r\n'));good(text.replace('\n','\nunrelated console line\n'))
good('');good('other console output and legacy DATAHDR lines only\n')
reject=row('RXREJECT',g=1,reason=12,count=1);reject2=change(reject,'count',2);total=row('RXTOTAL',g=1,reason=12,count=5)
dhcp=row('DHCPTOTAL',g=1,offer=1,ack=0,nak=0,other_offer=0,other_ack=0,other_nak=0)
match=row('DHCPMATCH',g=1,type=2,matched=1,tracked=1,count=1)
sample=row('RXCOUNTS',g=1,sample=1,uni=0,group=1,error=0,peer=0,key=0,replay=2,plain=0)
phy=row('TXPHY',g=1,n=1,pid=127,status=3,response=2,table=3,width=1,power=254,delay=305419896,applied=2309737967)
r=good(reject+reject2+sample+phy+match+total+dhcp);assert r['attempts'][0]['observed_reject_totals_covered']
r=good(reject+dhcp);assert r['attempts'][0]['terminal_summary_observed'] and not r['attempts'][0]['observed_reject_totals_covered']
# Families are independent: no invented TXS companion, zero-rejection totals,
# mandatory sample, matching reply, or terminal record is required for validity.
for line in (phy,match,total,dhcp,sample,reject):good(line)
good(reject+change(reject,'g',2)+sample+change(sample,'g',2))
for value in (0,14):bad(change(reject,'reason',value))
bad(change(reject,'count',0));bad(reject+reject2+change(reject,'count',3));bad(reject2+reject)
bad(reject+reject2+change(total,'count',1));bad(total+total);bad(total+change(total,'reason',11));bad(total+reject);bad(dhcp+sample);bad(dhcp+dhcp)
bad(reject+change(sample,'replay',0));bad(sample+change(sample,'sample',2));bad(sample+change(change(sample,'sample',2),'group',0))
for i,threshold in enumerate(m['THRESHOLDS'],1):bad(change(change(sample,'sample',i),'group',threshold-1))
bad(sample+sample);bad(change(sample,'sample',6))
for key in ('pid','status','response','table','width','power'):bad(change(phy,key,256))
for value in (0,57):bad(change(phy,'n',value))
bad(phy+phy);bad(change(phy,'n',2)+phy)
good(''.join(change(phy,'n',i) for i in range(1,25)));bad(''.join(change(phy,'n',i) for i in range(1,26)))
for key,values in [('type',(0,9)),('matched',(2,)),('tracked',(5,)),('count',(0,))]:
 for value in values:bad(change(match,key,value))
bad(change(match,'tracked',0));bad(match+match);bad(change(match,'tracked',4)+change(change(match,'count',2),'tracked',3));bad(match+change(dhcp,'offer',0))
good(''.join(change(match,'count',i) for i in range(1,13)));bad(''.join(change(match,'count',i) for i in range(1,14)))
# Numeric counts saturate; seeded helper fixtures are still observations.
maxg=(1<<64)-1;maxu=(1<<32)-1
good(change(change(phy,'g',maxg),'delay',maxu));good(change(change(sample,'g',maxg),'group',maxu));good(change(change(match,'g',maxg),'count',maxu))
good(change(change(reject,'count',maxu),'g',maxg)*2)
good(change(change(dhcp,'g',maxg),'offer',maxu))
bad(''.join(change(reject,'g',i) for i in range(1,66)))
bad('NORD2 WIFI TXPHY_EXTRA: FAKESECRET\n');bad(phy.replace('n=1','n=01'))
source=args.emitter_source or (R/'shim/nord2-xnu/wifi_station_data_diag.c' if R else None);assert source and source.is_file()
formats=re.findall(r'printf\("(NORD2 WIFI (?:RXREJECT|RXTOTAL|RXCOUNTS|TXPHY|DHCPMATCH|DHCPTOTAL)[^"\n]+)"',source.read_text());assert len(formats)==6
for fmt in formats:
 kind=fmt.split(':')[0].split()[-1];expected='NORD2 WIFI '+kind+': '+' '.join(k+'='+('%llu' if k=='g' else '%u') for k in m['SCHEMAS'][kind].split())+r'\n';assert fmt==expected,'source schema drift'
 assert '%s' not in fmt and '%p' not in fmt and not any(k+'=' in fmt for k in ('ssid','mac','nonce','mic','xid','client','payload'))
report={'passed':True,'positive':positive,'negative':negative,'actual_emitter_schemas':len(formats),'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'synthetic_fixtures_only':True,'hardware_lease_or_delivery_claim':False}
if args.log_dir:args.log_dir.mkdir(parents=True,exist_ok=True);(args.log_dir/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report))
