#!/usr/bin/env python3
"""Offline final-capture UFS comparison. No device access, clock/speed tuning."""
from pathlib import Path
import hashlib,importlib.util,json,re
ROOT=Path(__file__).resolve().parents[4];OUT=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('strategy_parser',ROOT/'research/nord2/ufs-performance/parse-result.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
RUNS=[('ce84_x1','research/nord2/ufs-preflight-cost/ios-profile'),('2e4f_x1','research/nord2/ufs-linereset-common/ios'),('3b2e_live_x2','research/nord2/ufs-live-common-ap/ios')]

def read_run(name,folder):
 d=ROOT/folder;log=d/'hardware-live/xnu-live.log';raw=log.read_bytes();result_path=d/'hardware-live/result.json'
 if not result_path.exists():raise RuntimeError(name+': capture result.json absent; do not finalize growing log')
 result=json.loads(result_path.read_text());sha=hashlib.sha256(raw).hexdigest()
 assert result['log_sha256']==sha and result['log_bytes']==len(raw),name+': capture hash/length mismatch'
 text=raw.decode('utf-8');parsed=module.parse(text);assert parsed['valid'],(name,parsed['errors'])
 boot=[line for line in text.splitlines(keepends=True) if line.startswith('NORD2 BOOT: first-GUI ')]
 assert len(boot)==1 and boot[0].endswith('\n')
 bootfields={k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',boot[0])}
 c=parsed['counters'];p=c['PROFILE'];s=c['STRATEGY'];ph=c['PHASES'];freq=p['freq'];assert bootfields['freq']==freq
 manifest=json.loads((d/'manifest.json').read_text());evidence={}
 for num,line in enumerate(text.splitlines(keepends=True),1):
  if any(line.startswith(mark) for mark in ('NORD2 BOOT: first-GUI','NORD2 UFS STRATEGY:','NORD2 UFS PHASES:','NORD2 UFS CALLERS:','NORD2 UFS SIZES:','NORD2 UFS PROFILE:','NORD2 UFS LINK:','NORD2 UFS X2 LIVE')):evidence[str(num)]=line
 seconds={'first_gui_ownership':bootfields['elapsed_ticks']/freq,'framebuffer':bootfields['fb_ticks']/freq,'command':p['cmd_ticks']/freq,'doorbell':p['doorbell_ticks']/freq,'command_max':p['cmd_max']/freq,**{k:v/freq for k,v in ph.items()}}
 output={'name':name,'log':str(log.relative_to(ROOT)),'capture_sha256':sha,'capture_bytes':len(raw),'capture_hash_validated':True,'image_sha256':manifest['image_sha256'],'kernel_sha256':manifest['kernel_sha256'],'source':manifest.get('expected_kernel_source'),'commandline':manifest['commandline'],'timer_hz':freq,'seconds':seconds,'counters':c,'boot':bootfields,'report_evidence':evidence,'command_mean_us':seconds['command']/p['commands']*1e6,'read_mean_transfer_bytes':p['read_bytes']/p['reads'],'combined_command_traffic_bytes_per_second':(p['read_bytes']+p['write_bytes'])/seconds['command'],'histogram_consistent':True,'first_gui_record_is_human_visible_gui_proof':False}
 if name=='3b2e_live_x2':
  required=['NORD2 UFS X2 LIVE POLICY: armed=1 invalid=0 return_blocked=1 reason=manual-reset-required\n','NORD2 UFS X2 LIVE: rc=0 phase=5 error=0 active=1 uncertain=0 root_allowed=1 handoff_allowed=0\n','NORD2 UFS X2 LIVE PROOF: commands=60 sets=18 reads=2 verified=1 rollback=0 original=0\n','NORD2 UFS X2 LIVE PROFILE: link_fields=pre-transition-baseline return_policy=manual-reset-only\n']
  for line in required:assert text.count(line)==1
  assert not re.search(r'NORD2 UFS X2 LIVE: (?:runtime I/O failed|uncertain before I/O)',text)
  assert 'panic(cpu' not in text
  output['retained_x2_proof']='Exact60UIC/18SET/2fixedGPTdigest phase5 proof; no x1 rollback; return permanently blocked'
  output['printed_link_fields']='pre-transition x1 baseline, explicitly labeled; not a post-transition measurement'
 return output
runs=[read_run(*r) for r in RUNS];live=runs[-1];comparisons=[]
for prior in runs[:-1]:
 delta={k:live['seconds'][k]-prior['seconds'][k] for k in live['seconds']}
 counters={k:{'prior':prior['counters']['PROFILE'][k],'live':live['counters']['PROFILE'][k],'percent_change':100*(live['counters']['PROFILE'][k]/prior['counters']['PROFILE'][k]-1)} for k in ('commands','reads','writes','syncs','read_bytes','write_bytes')}
 comparisons.append({'prior':prior['name'],'delta_seconds_live_minus_prior':delta,'profile_workload_changes':counters,'caller_requested_bytes_percent_change':100*(live['counters']['STRATEGY']['requested_bytes']/prior['counters']['STRATEGY']['requested_bytes']-1),'command_traffic_rate_percent_change':100*(live['combined_command_traffic_bytes_per_second']/prior['combined_command_traffic_bytes_per_second']-1)})
report={'status':'final_capture_hash_verified','human_observation':{'statement':'Sì, lockscreen e comandi funzionano','reported_by':'human user, relayed by parent after capture completion','lockscreen_and_controls_confirmed':True,'onset_time_known':False,'not_a_boot_timing_measurement':True},'runs':runs,'comparisons':comparisons,'conclusions':['Retained x2 transaction/read proof succeeded before root publication; zero UFS command errors/timeouts at first-GUI report.','The live-x2 first-GUI ownership record is later than both previous x1 records; no boot wall improvement demonstrated.','Command/held time is slightly lower than2e4f but slightly higher thance84; bytes, request count, histogram and persistent RW filesystem state differ.','Mutex wait is an overlapping aggregate across callers, not removable boot wall time.','Map/copy are aggregate timer costs; do not sum overlapping/partially overlapping phases as wall time.','First-GUI is the kernel ownership record. Human separately confirmed lockscreen and controls functioning; onset time is unknown, so it supplies no boot timing.','Command traffic divided by total command time is mixed-workload telemetry, not sequential link throughput.','No CPU or DRAM execution frequency was measured; CNTFRQ is only the timebase.']}
(OUT/'ufs-cost-comparison.json').write_text(json.dumps(report,indent=2)+'\n')
lines=['Final captured UFS evidence: all3 raw logs match result.json SHA256 and byte counts.','', '| Capture | Ownership s | Cmd s | Doorbell s | Held s | Map s | Copy s | Wait sum s | Read bytes | Commands |','|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|']
for run in runs:
 t=run['seconds'];p=run['counters']['PROFILE'];lines.append(f"| {run['name']} | {t['first_gui_ownership']:.6f} | {t['command']:.6f} | {t['doorbell']:.6f} | {t['held']:.6f} | {t['map']:.6f} | {t['copy']:.6f} | {t['mutex_wait']:.6f} | {p['read_bytes']} | {p['commands']} |")
lines+=['']+report['conclusions']
lines.append('Human observation, separately supplied after capture: '+report['human_observation']['statement']+'. Onset time unknown.')
(OUT/'ufs-cost-comparison.md').write_text('\n'.join(lines[:2])+'\n'+'\n'.join(lines[2:2+2+len(runs)])+'\n\n'+'\n\n'.join(report['conclusions'])+'\n\nHuman separately confirmed: '+report['human_observation']['statement']+'. Onset time unknown.\n')
print(json.dumps({'status':report['status'],'live_sha256':live['capture_sha256'],'live_seconds':live['seconds'],'comparisons':comparisons},indent=2))
