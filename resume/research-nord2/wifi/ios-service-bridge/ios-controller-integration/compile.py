from pathlib import Path
import json,subprocess,tempfile,hashlib
P=Path(__file__).resolve().parent;B=P.parent;R=next(x for x in P.parents if x.name=='ios-sweet')
cpp=json.loads((B/'native-lifecycle-shell-v2/arm64-command.json').read_text())['argv'];cbase=next(x['argv'] for x in json.loads((R/'research/nord2/wifi-rx-lifecycle/ios/focused/commands.json').read_text()) if x['name']=='wifi_default_feature')
cs=[P/'wifi_ios_policy.c',B/'consumer-integration/niw_rate_compat.c',B/'bind-transport-policy/binding_contract.c',B/'control_contract.c',B/'event-pipe/pipe.c',B/'lifecycle-scan-adapter/v2/lifecycle_scan.c',B/'completed-scan-adapter-age/age.c',B/'completed-scan-adapter/snapshot.c',B/'scan-backend-boundary/backend_bss.c',B/'scan-and-initialization/scan_contract.c',B/'initial-query-audit/cache_cursor.c',B/'passive-scan-contract/v2/mapping.c',B/'card-capability-contract/v2/card.c']
records=[]
with tempfile.TemporaryDirectory(prefix='niwl-integrated-') as td:
 for source in [P/'LifecycleShell.cpp',P/'Registry.cpp',*cs]:
  cmd=(cpp if source.suffix=='.cpp' else cbase)[:]
  old='LifecycleShell.cpp' if source.suffix=='.cpp' else 'wifi_default_feature.c';cmd[next(i for i,x in enumerate(cmd) if x.endswith(old))]=str(source);output=Path(td)/(source.stem+'.o');cmd[cmd.index('-o')+1]=str(output)
  cmd[1:1]=['-I'+str(R/'shim/nord2-xnu'),'-I'+str(B),'-I'+str(B/'passive-scan-contract/v2')]
  z=subprocess.run(cmd,capture_output=True,text=True);(P/(source.name+'.compile.log')).write_text(z.stdout+z.stderr)
  print(source.name,z.returncode);z.check_returncode();records.append({'source':str(source),'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'argv':cmd,'returncode':z.returncode,'object_sha256':hashlib.sha256(output.read_bytes()).hexdigest()})
(P/'compile-record.json').write_text(json.dumps(records,indent=2)+'\n')
