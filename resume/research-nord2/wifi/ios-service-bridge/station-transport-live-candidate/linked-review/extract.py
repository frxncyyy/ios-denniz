#!/usr/bin/env python3
"""Bounded station/service extraction only. Never grants a gate or linked PASS."""
from pathlib import Path
import argparse,hashlib,json,re,runpy,subprocess
P=Path(__file__).resolve().parent
R=next(p for p in P.parents if (p/'shim/nord2-xnu').is_dir())
MachO=runpy.run_path(str(R/'research/nord2/wifi-ios-profile/ios/linked-review/review.py'))['MachO']
SELECTED={
 'adopt':'_nst_prepare','transport':'_nst_step','port':'_nst_authorize',
 'cleanup':'_nst_cleanup_begin','receipts':'_nwn_event','typed_completion':'_nwo_complete',
 'worker':'_ncd_worker','lease':'_ncd_normal_owner_window',
 'disconnect':'_nord2_wifi_join_disconnect'}
SOURCES=['wifi_station_transport.c','wifi_station_transport.h','wifi_station_transport_bind.inc',
 'wifi_station_service_bind.inc','wifi_scan_service_bind.inc','wifi_wpa_nst.c',
 'wifi_wpa_owner.c','wifi_station_transaction.c','wifi_basic_config_bind.inc',
 'wifi_normal_backend_forward.inc']
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for a in ['source-commit','ci-run','kernel','run-json','sha256','bytes']:p.add_argument('--'+a)
 p.add_argument('--symbol',action='append',default=[],help='bounded replacement LABEL=EXACT_SYMBOL after compiler inlining review')
 a=p.parse_args();selected=SELECTED.copy()
 for v in a.symbol:
  label,symbol=v.split('=',1);assert label in selected;selected[label]=symbol
 plan={'status':'UNBOUND_PLAN','linked_verdict':None,'selected':selected,
       'limits':'nine functions, <=262144 total code bytes; compiler inlining/missing symbol requires explicit bounded adjustment; no inherited PASS',
       'owner_audits':'root owns full source/artifact identity, flags and ISA; this extracts branches/calls only'}
 if not any(vars(a)[k.replace('-','_')] for k in ['source-commit','ci-run','kernel','run-json','sha256','bytes']):
  (P/'plan.json').write_text(json.dumps(plan,indent=2)+'\n');print('UNBOUND_PLAN; no artifact or linked verdict');return
 assert all(getattr(a,k) for k in ['source_commit','ci_run','kernel','run_json','sha256','bytes'])
 commit=subprocess.check_output(['git','-C',str(R),'rev-parse',a.source_commit+'^{commit}'],text=True).strip()
 m=json.loads(Path(a.run_json).read_text());k=Path(a.kernel).resolve()
 assert m['databaseId']==int(a.ci_run) and m['headSha']==commit and m['conclusion']=='success'
 assert k.stat().st_size==int(a.bytes) and sha(k)==a.sha256
 sources={n:hashlib.sha256(subprocess.check_output(['git','-C',str(R),'show',commit+':shim/nord2-xnu/'+n])).hexdigest() for n in SOURCES}
 linked=MachO(k);out=P/'artifact';out.mkdir(exist_ok=True);rows={};missing={};total=0
 for label,symbol in selected.items():
  try:start,end,name=linked.bounds(symbol,True)
  except AssertionError as e:missing[label]=str(e);continue
  total+=end-start;assert total<=262144
  body=linked.read(start,end-start);text=linked.dump(start,end);(out/(label+'.txt')).write_text(text)
  calls=[]
  for hit in re.finditer(r'\bbl\s+(0x[0-9a-f]+)',text):
   v=int(hit[1],16);calls.append({'target':hex(v),'symbols':sorted({n for va,_,n in linked.syms if va==v})})
  rows[label]={'symbol':name,'start':hex(start),'end':hex(end),'bytes':end-start,'code_sha256':hashlib.sha256(body).hexdigest(),'dump_sha256':sha(out/(label+'.txt')),'direct_calls':calls}
 result={'status':'EXTRACTED_PENDING_MANUAL_REVIEW','linked_verdict':None,'source_commit':commit,'ci_run':int(a.ci_run),'kernel_sha256':sha(k),'kernel_bytes':k.stat().st_size,'run_json_sha256':sha(Path(a.run_json)),'functions':rows,'unresolved_symbols':missing,'total_code_bytes':total,'sources':sources,'limitations':'No automatic source/object semantic match. Read actual branches/constants/calls; compiler/inlining mismatch is not itself a bug. Missing functions are unresolved, never PASS.'}
 (out/'extraction.json').write_text(json.dumps(result,indent=2)+'\n');print('EXTRACTED_PENDING_MANUAL_REVIEW',len(rows),'functions;',len(missing),'unresolved;',total,'code bytes')
if __name__=='__main__':main()
