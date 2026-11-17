#!/usr/bin/env python3
"""Offline closed-template and exact sole-identity verifier tests; no runner/build/device."""
import ast,hashlib,importlib.util,json,os,subprocess,sys
from pathlib import Path
sys.dont_write_bytecode=True
P=Path(__file__).resolve().parent;R=P.parents[3]
def read(p):return json.loads(p.read_text())
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def module(n,p):
 s=importlib.util.spec_from_file_location(n,p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
checks=0
def ck(ok):
 global checks
 checks+=1
 assert ok
i=read(P/'identity-required.json');ck(i['parent_verified']is False)
ck(all(i[k]is None for k in ['ci_run','ci_attempt','source_commit','kernel_sha256','kernel_bytes','kernel_path','ci_run_json']))
n=read(P/'native-schema-required.json');ck(n['parent_verified']is False and n['schema_pending']is True)
ck(n['source_pins']==n['linked_reviews']==n['connection_diagnostics']==[])
prepared=read(P/'prepared-image-required.json');ck(prepared['parent_verified']is False and prepared['image_bytes']is None and prepared['image_sha256']is None)
a=read(P/'authorization-required.json');ck(all(a[k]is False for k in ['human_exact_image_authorized','rf_calibration_authorized','conditional_recovery_authorized']))
ck(a['image_sha256']is None and a['device_serial']is None and a['scope']=='boot-with-wifi-ios-join-options')
s=read(P/'stage-pending.json');ck(all(s[k]is False for k in ['source_verified','native_verified','prepared_verified','image_authorized','runner_used','hardware_tested']))
for name in ['RUNNER_EXECUTION_USED','manifest.json','payload.bin','image-build.log','flash.log','hardware-live','client-hardware.log','client-hardware.pid']:
 ck(not(P/name).exists())
for f in P.glob('*.py'):ast.parse(f.read_text());checks+=1
sh=subprocess.run(['sh','-n',str(P/'build-image.sh')],capture_output=True,text=True);ck(sh.returncode==0)
env=os.environ.copy();env['PYTHONDONTWRITEBYTECODE']='1'
blocked=subprocess.run([sys.executable,str(P/'verify-image.py'),'--preflight'],capture_output=True,text=True,env=env)
ck(blocked.returncode!=0 and 'BLOCKED: parent has not supplied and verified CI identity' in blocked.stderr)
ck(not(P/'image-build.log').exists())
v=module('identity_stage_verify',P/'verify-image.py');ck(len(v.COMMANDLINE.encode())==539)
ck(v.IMAGE.name=='nord2-ios-wifi-join-options-stop23.img')
ck(read(P/'baseline.json')['future_boot_arg']=='nord2wifibasic=1 nord2wifiscan=1 nord2wifivcn33=1 nord2wifiios=1')
origin=read(P/'template-origins.json');old=R/origin['origin']
for name in ['parse-backend.py','parse-ios-service.py','core-receipt.py','core-lifecycle.py','parser-dependency.json']:
 ck(sha(P/name)==origin['copied_allowlist'][name]==sha(old/name))
ck(sha(P/'parse-connection.py')==sha(R/'tools/nord2-wifi-connection-log.py'))
recovery=read(P/'recovery-image.json');ck(recovery['sha256']=='d9a44316f6b257221a948a32483a6778b33cdf5057a3f79ffecfceebf0318de3' and recovery['bytes']==33462272)
ck(read(P/'baseline.json')['secondary_recovery_image']['sha256']=='6e4ce860cfbd0b27fbace65e61e96edc8d2af8067603652cd67a0939c53331c6')
sys.path.insert(0,str(R/'tools'));import adt,struct
baseline=read(P/'baseline.json');payload=(R/baseline['identity_baseline_payload_path']).read_bytes()
ck(hashlib.sha256(payload).hexdigest()==baseline['identity_baseline_payload_sha256'])
off,size=struct.unpack_from('<QQ',payload,32);before=payload[off:off+size]
ck(size==19160 and hashlib.sha256(before).hexdigest()==baseline['identity_baseline_adt_sha256'])
cal=(R/'research/nord2/wifi/calibration/WIFI').read_bytes()
tool=module('identity_stage_tool',R/'tools/nord2-wifi-identity.py');after=tool.transform(before)
v.verify_identity_addition(before,after,cal);checks+=1
ck(len(after)==len(before)+44)
rejected=0
for case in range(12):
 tree,_=adt.parse(after);chosen=next(x for x in tree.children if x.name=='chosen');wlan=next(x for x in next(x for x in tree.children if x.name=='arm-io').children if x.name=='wlan')
 if case==0:chosen.props=[(k,f,bytes([val[0]^2])+val[1:] if k=='mac-address-wifi0' else val)for k,f,val in chosen.props]
 if case==1:chosen.props=[(k,f,b'not raw six bytes' if k=='mac-address-wifi0' else val)for k,f,val in chosen.props]
 if case==2:chosen.props=[(k,1 if k=='mac-address-wifi0' else f,val)for k,f,val in chosen.props]
 if case==3:chosen.props.append(('mac-address-wifi0',0,cal[4:10]))
 if case==4:chosen.props.append(('unrelated-new-property',0,b'x'))
 if case==5:tree.props.append(('unrelated-root-property',0,b'x'))
 if case==6:wlan.props.append(('unrelated-wlan-property',0,b'x'))
 if case==7:chosen.children.append(adt.Node())
 if case==8:chosen.props=[p for p in chosen.props if p[0]!='nord2-wifi-calibration']
 if case==9:tree.children.append(chosen)
 if case==10:wlan.props=[(k,f,b'wrong-device\0' if k=='compatible' else val)for k,f,val in wlan.props]
 corrupt=adt.serialize(tree)+(b'junk' if case==11 else b'')
 try:v.verify_identity_addition(before,corrupt,cal)
 except (AssertionError,ValueError,UnicodeDecodeError,struct.error):rejected+=1
ck(rejected==12)
for name in ['RUNNER_EXECUTION_USED','manifest.json','payload.bin','image-build.log','flash.log']:ck(not(P/name).exists())
report=dict(verdict='PASS_CLOSED_TEMPLATE_AND_SOLE_IDENTITY_ADDITION',checks=checks,positive_actual_calibrated_adt=1,negative_mutations=rejected,
 commandline_bytes=539,source_ci_kernel_unknown=True,all_source_native_prepared_authorization_gates_false=True,
 old_evidence_unchanged=True,runner_or_builder_invoked=False,device_access=False,
 no_address_or_calibration_values_printed=True,identity_tool_sha256=sha(R/'tools/nord2-wifi-identity.py'),
 private_actual_calibrated_baseline_sha256=hashlib.sha256(before).hexdigest())
(P/'test-stage-short-parser-report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
