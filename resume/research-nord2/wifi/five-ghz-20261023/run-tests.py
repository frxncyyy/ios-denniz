#!/usr/bin/env python3
from pathlib import Path
import argparse,hashlib,json,os,subprocess,tempfile
P=Path(__file__).resolve().parent;R=next(p for p in P.parents if (p/'AGENTS.md').exists());S=R/'shim/nord2-xnu';C=P/'proposed/shim/nord2-xnu'
a=argparse.ArgumentParser();a.add_argument('--output',type=Path,required=True);x=a.parse_args();O=x.output.resolve();O.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
files=list(S.glob('*'))+list(C.glob('*'));before={str(p):sha(p) for p in files if p.is_file()}
names='wifi_normal_config wifi_normal_init wifi_regulatory_policy wifi_ios_scalar wifi_ios_policy wifi_ios_snapshot wifi_ios_bss wifi_ios_rates wifi_ios_mapping wifi_ios_scan_contract wifi_ios_cursor wifi_scan_bss'
env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
with tempfile.TemporaryDirectory(prefix='wifi-fiveGHz-') as td:
 d=Path(td)
 for s in S.iterdir():
  if s.is_file():(d/s.name).write_bytes(s.read_bytes())
 for s in C.iterdir():(d/s.name).write_bytes(s.read_bytes())
 cmd=['clang','-std=c11','-O1','-g','-DNCFG_HOST_TEST','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(d),str(P/'tests/channels.c')]+[str(d/(n+'.c')) for n in names.split()]+['-lcrypto','-o',str(d/'test')]
 subprocess.run(cmd,check=True,env=env)
 run=subprocess.run([str(d/'test'),str(R/'research/nord2/wifi/calibration/WIFI')],text=True,capture_output=True,env=env)
 (O/'channels.log').write_text(run.stdout+run.stderr);print(run.stdout,end='');run.check_returncode()
 # Actual retained native owner runs boot26 and queued scan recipe; only radio is synthetic.
 script=(R/'tools/test-nord2-wifi-join-owner.py').read_text()
 old='setup(0);run_worker();assert(nss_native.ready'
 new='''setup(0);run_worker();assert(nss_native.info.count==37);unsigned dfs_count=0;for(unsigned i=0;i<nss_native.info.count;i++)dfs_count+=(unsigned)((nss_native.info.dfs_mask>>i)&1);assert(dfs_count==15);assert(ncb_native.plan.channel_count==37);assert(ncb_native.plan.scan_frames.scan[66]==0);assert(ncb_native.plan.scan_frames.scan[64+159]==32&&ncb_native.plan.scan_frames.scan[64+826]==5);assert(nss_native.ready'''
 assert script.count(old)==1;script=script.replace(old,new)
 old='P=Path(__file__).resolve().parent;R=next(p for p in P.parents if (p/\'AGENTS.md\').exists());'
 assert script.count(old)==1;script=script.replace(old,'P=Path('+repr(str(R/'tools'))+');R=Path('+repr(str(R))+');')
 runner=O/'owner-runner.py';runner.write_text(script)
 run=subprocess.run(['python3',str(runner),'--candidate',str(C),'--log-dir',str(O/'owner')],text=True,capture_output=True,env=env)
 (O/'owner.log').write_text(run.stdout+run.stderr);print(run.stdout[-1500:],end='');run.check_returncode()
 # Replay all existing production iOS contracts against exactly this overlay.
 suites={
  'join':('wifi_ios_join',),
  'station-view':('wifi_ios_station_view','wifi_ios_join','wifi_ios_policy','wifi_ios_mapping','wifi_ios_rates','wifi_ios_scan_contract'),
  'snapshot':('wifi_ios_snapshot','wifi_ios_age','wifi_ios_bss','wifi_ios_rates','wifi_ios_mapping','wifi_ios_scan_contract','wifi_ios_cursor','wifi_scan_bss'),
  'policy':('wifi_ios_policy','wifi_ios_rates','wifi_ios_mapping'),
  'scalar':('wifi_ios_scalar',),
  'caller':('wifi_ios_policy','wifi_ios_rates','wifi_ios_mapping'),
 }
 contract_summaries=[]
 for name,units in suites.items():
  exe=d/('contract-'+name)
  cmd=['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(d),str(R/'tools/fixtures/nord2-wifi-ios-contract'/('test-'+name+'.c'))]+[str(d/(u+'.c')) for u in units]+['-o',str(exe)]
  subprocess.run(cmd,env=env,check=True)
  run=subprocess.run([str(exe)],env=env,capture_output=True,text=True)
  (O/('contract-'+name+'.log')).write_text(run.stdout+run.stderr);run.check_returncode();contract_summaries.append(run.stdout.strip())
 print('all6 existing canonical iOS contract suites PASS (including join and station view)')
assert before=={str(p):sha(p) for p in files if p.is_file()},'source changed during tests'
(O/'report.json').write_text(json.dumps({'status':'PASS_SYNTHETIC_ONLY','sanitizers':['ASan','UBSan'],'source_inputs_unchanged':True,'contract_summaries':contract_summaries,'private_values_printed':False,'hardware_tested':False,'candidate':{p.name:sha(p) for p in C.iterdir()},'scope':['production configuration+passive admission+channel ABI+beacon/snapshot','actual native owner boot26 and6 queued scans; DFS37/15 and extended channel list']},indent=2)+'\n')
