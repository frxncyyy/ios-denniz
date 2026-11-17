#!/usr/bin/env python3
"""Strict supplemental numeric observations; never air/BSD/DHCP lease proof."""
import argparse,json,re,sys
U32=(1<<32)-1;U64=(1<<64)-1
SCHEMAS={
 'RXREJECT':'g reason count',
 'RXTOTAL':'g reason count',
 'RXCOUNTS':'g sample uni group error peer key replay plain',
 'TXPHY':'g n pid status response table width power delay applied',
 'DHCPMATCH':'g type matched tracked count',
 'DHCPTOTAL':'g offer ack nak other_offer other_ack other_nak'}
NUM=r'(0|[1-9][0-9]*)'
PAT={k:re.compile('^NORD2 WIFI '+k+': '+' '.join(f+'='+NUM for f in fields.split())+'$') for k,fields in SCHEMAS.items()}
DETECT=re.compile(r'NORD2 WIFI (?:RXREJECT|RXTOTAL|RXCOUNTS|TXPHY|DHCPMATCH|DHCPTOTAL)')
REASONS={2:'error',6:'peer',11:'key',12:'replay',13:'plain'}
DHCP={2:'offer',5:'ack',6:'nak'}
THRESHOLDS=(1,32,256,1024,4096)
def require(ok,why):
 if not ok:raise ValueError(why)
def parse(text):
 require(isinstance(text,str) and len(text)<=16*1024*1024,'input type/size bound')
 streams={};observed=0
 for raw in text.splitlines(keepends=True):
  line=raw.rstrip('\r\n')
  if not DETECT.search(line):continue
  require(raw.endswith('\n') and len(raw.encode())<=255,'truncated/oversized supplemental row')
  key=x=None
  for kind,pattern in PAT.items():
   m=pattern.fullmatch(line)
   if m:key=kind;x=dict(zip(SCHEMAS[kind].split(),map(int,m.groups())));break
  require(x is not None,'unknown/malformed supplemental schema')
  require(0<x['g']<=U64 and all(v<=U32 for k,v in x.items() if k!='g'),'integer domain')
  g=x['g'];require(g in streams or len(streams)<64,'generation resource bound')
  s=streams.setdefault(g,{'generation':g,'reject':{},'totals':{},'snapshots':[],'phy':[],'matches':[],'dhcp_total':None,'terminal_started':False})
  require(s['dhcp_total'] is None,'record after terminal DHCP summary')
  if key=='RXREJECT':
   require(not s['terminal_started'] and 1<=x['reason']<=13 and x['count']>0,'reject domain/order')
   prev=s['reject'].setdefault(x['reason'],[])
   require(len(prev)<2 and (not prev or x['count']>=min(U32,prev[-1]['count']+1)),'reject budget/counter')
   if s['snapshots'] and x['reason'] in REASONS:require(x['count']>=s['snapshots'][-1][REASONS[x['reason']]],'reject counter rollback')
   prev.append(x)
  elif key=='RXTOTAL':
   require(1<=x['reason']<=13 and x['count']>0 and (not s['totals'] or x['reason']>max(s['totals'])),'reject total domain/order/duplicate')
   require(all(z['count']<=x['count'] for z in s['reject'].get(x['reason'],[])),'reject total below observations')
   if s['snapshots'] and x['reason'] in REASONS:require(s['snapshots'][-1][REASONS[x['reason']]]<=x['count'],'reject total below snapshot')
   s['terminal_started']=True;s['totals'][x['reason']]=x['count']
  elif key=='RXCOUNTS':
   require(not s['terminal_started'] and 1<=x['sample']<=5 and x['uni']+x['group']>=THRESHOLDS[x['sample']-1],'snapshot domain/threshold/order')
   if s['snapshots']:
    prev=s['snapshots'][-1];require(x['sample']>prev['sample'] and all(x[k]>=prev[k] for k in ('uni','group','error','peer','key','replay','plain')),'snapshot duplicate/rollback')
   for reason,name in REASONS.items():require(all(z['count']<=x[name] for z in s['reject'].get(reason,[])),'snapshot below observed rejects')
   s['snapshots'].append(x)
  elif key=='TXPHY':
   require(not s['terminal_started'] and 1<=x['n']<=56 and all(x[k]<=255 for k in ('pid','status','response','table','width','power')),'PHY domain/order')
   require(len(s['phy'])<24 and (not s['phy'] or x['n']>s['phy'][-1]['n']),'PHY budget/serial')
   s['phy'].append(x)
  elif key=='DHCPMATCH':
   require(not s['terminal_started'] and 1<=x['type']<=8 and x['matched']<=1 and x['tracked']<=4 and x['count']>0 and (not x['matched'] or x['tracked']>0),'DHCP match domain/order')
   require(len(s['matches'])<12,'DHCP match budget')
   if s['matches']:require(x['tracked']>=s['matches'][-1]['tracked'],'DHCP tracked rollback')
   prev=[z for z in s['matches'] if (z['type'],z['matched'])==(x['type'],x['matched'])]
   require(not prev or x['count']>=min(U32,prev[-1]['count']+1),'DHCP match counter rollback')
   s['matches'].append(x)
  else:
   for z in s['matches']:
    if z['type'] in DHCP:require(z['count']<=x[('' if z['matched'] else 'other_')+DHCP[z['type']]],'DHCP total below observations')
   s['terminal_started']=True;s['dhcp_total']=x
  observed+=1
 for s in streams.values():
  known=set(s['reject'])
  if s['snapshots']:
   known|={reason for reason,name in REASONS.items() if s['snapshots'][-1][name]}
  s['terminal_summary_observed']=s['dhcp_total'] is not None
  # Missing whole lines remain missing observations, never inferred zeroes.
  s['observed_reject_totals_covered']=bool(s['dhcp_total']) and known<=set(s['totals'])
  s['rejection_log_budget_exhausted']=[k for k,v in s['reject'].items() if len(v)==2]
  s['dhcp_log_budget_exhausted']=len(s['matches'])==12
  s['phy_log_budget_exhausted']=len(s['phy'])==24
  s.pop('terminal_started')
 return {'valid':True,'observed_records':observed,'attempts':list(streams.values()),
  'independent_record_families':True,'absence_proves_zero_events':False,
  'capture_integrity_proven':False,'rx_is_bsd_delivery':False,'dhcp_is_lease_proof':False,
  'dhcp_is_authenticated':False,'internet_proven':False,'air_delivery_proven':False,
  'shared_pid127_ambiguous':True,'last_publication_is_packet_correlation':False,
  'matched_tuple_is_only_bounded_observation':True,'identities_or_xids_exposed':False}
def main():
 a=argparse.ArgumentParser(description=__doc__);a.add_argument('log');v=a.parse_args()
 try:print(json.dumps(parse(open(v.log).read()),indent=2))
 except (OSError,UnicodeError,ValueError) as e:print(str(e),file=sys.stderr);return 1
 return 0
if __name__=='__main__':sys.exit(main())
