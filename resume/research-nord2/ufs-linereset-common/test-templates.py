#!/usr/bin/env python3
from pathlib import Path
import ast,json,subprocess
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2]
source='9327714bfe56719881e4c6251243fcd6afc97b24'
for name,oldname in [('diagnostic','diagnostic'),('ios','ios-profile')]:
 out=HERE/name;template=json.loads((out/'template.json').read_text())
 old=json.loads((ROOT/'research/nord2/ufs-preflight-cost'/oldname/'manifest.json').read_text())
 assert template['commandline']==old['commandline']+(' nord2wificommon=1' if name=='ios' else '')
 assert len(template['commandline'].encode())<608
 assert template['expected_source']==source and template['ci_run']==39506694221
 assert template['kernel_sha256']=='PENDING_PARENT_VERIFIED_KERNEL_SHA256'
 tree=ast.parse((out/'verify-image.py').read_text());constants={n.targets[0].id:ast.literal_eval(n.value) for n in tree.body if isinstance(n,ast.Assign) and isinstance(n.targets[0],ast.Name) and isinstance(n.value,ast.Constant)}
 assert constants['COMMANDLINE']==template['commandline'] and constants['SOURCE']==source
 assert constants['EXPECTED_KERNEL_SHA256']==template['kernel_sha256']
 text=(out/'verify-image.py').read_text();build=(out/'build-image.sh').read_text()
 assert template['commandline'] in build and template['image'] in build
 assert 'kernel_hash==EXPECTED_KERNEL_SHA256' in text
 assert "payload[off:off+size]==expected[seg['fileoff']:seg['fileoff']+size]" in text
 assert 'segments_byte_verified=True' in text and 'SHA256SUMS' in text
 assert not (out/'manifest.json').exists() and not (out/'image-build.log').exists()
 assert not (ROOT/template['image']).exists()
 subprocess.run(['sh','-n',str(out/'build-image.sh')],check=True)
 denied=subprocess.run(['python3',str(out/'verify-image.py'),'--preflight'],capture_output=True,text=True)
 assert denied.returncode and 'parent must supply verified exact CI kernel SHA256' in denied.stderr
 if name=='diagnostic':
  assert len(template['commandline'].encode())==379
  assert '--allow-line-reset' in (out/'parse-report.sh').read_text()
  subprocess.run(['sh','-n',str(out/'parse-report.sh')],check=True)
print('PASS onlytemplates: exact prior379B RO/ce84RW+common, pendinghashblock, CI/source/schema/segmentguards, noimages/manifests/buildlogs or device actions')
# Execute only the pure manifest-request function extracted from each verifier:
# no prerequisite reads, unpacking, image generation or module main execution.
metadata=[]
for directory in ('diagnostic','ios'):
 tree=ast.parse((HERE/directory/'verify-image.py').read_text())
 fn=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='request_metadata')
 namespace={};exec(compile(ast.Module(body=[fn],type_ignores=[]),'request-metadata','exec'),namespace)
 metadata.append(ast.dump(fn,include_attributes=False))
 for mode in ('ufs_stop_only','ios_observer'):
  stop=mode=='ufs_stop_only';d=namespace['request_metadata'](mode)
  assert d['emi_snapshot_requested'] is False and d['emi_snapshot_query_count_max']==0
  assert d['strategy_cost_report_expected']==(not stop) and d['preflight_observability_requested']==stop
  assert d['firmware_execution'] is False and d['radio_networking_enabled'] is False
  assert d['cpu_clock_requested'] is False and d['zoom_reset_requested'] is False
  assert d['wifi_common_init_requested']==(not stop)
  assert d['wifi_common_register_mutation_requested']==(not stop)
  assert d['wifi_common_restore_every_touched_register_required']==(not stop)
  assert d['wifi_common_outer_power_restore_required']==(not stop)
  assert d['wifi_common_dirty_restore_blocks_handoff']==(not stop)
  assert d['wifi_common_dirty_restore_action']==(None if stop else 'retain_power_and_ownership_panic_block_handoff')
  assert d['wifi_common_stage_requested']==(None if stop else 2)
  assert d['wifi_common_runtime_result'] is None
  assert d['ufs_health_source_policy']=='line-reset-exact-v1'
  assert d['preflight_parser_policy']==('line-reset-exact-v1' if stop else None)
  assert d['preflight_parser_flag']==('--allow-line-reset' if stop else None)
  assert d['disk_writes_enabled']==(not stop) and d['ufs_link_transition_requested']==stop
  assert d['diagnostic_always_halts']==stop and d['linux_handoff_blocked']==stop
  assert d['physical_fastboot_recovery_required']==stop
  assert d['input_probe_requested']==(not stop) and d['firmware_ram_verification_requested']==(not stop)
  assert d['framebuffer_diagnostics_requested']==(not stop)
  assert d['successful_lane_transition_observed'] is False and d['firmware_ram_verified_on_hardware'] is False
 # Ensure the actual manifest receives this function and no later metadata overrides.
 main=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='main')
 calls=[n for n in ast.walk(main) if isinstance(n,ast.Call) and isinstance(n.func,ast.Attribute) and isinstance(n.func.value,ast.Name) and n.func.value.id=='report' and n.func.attr=='update']
 assert len(calls)==1 and ast.unparse(calls[0])=='report.update(request_metadata(MODE))'
 assert not any(isinstance(n,ast.Subscript) and isinstance(n.value,ast.Name) and n.value.id=='report' and isinstance(n.ctx,ast.Store) for n in ast.walk(main))
assert metadata[0]==metadata[1], 'verifier request metadata must stay identical for both modes'
print('PASS bothverifiers/bothmodebranches: EMIoff/query0, modeexactcost/preflight, commonmutation/fullrestore/dirtypanic-handoffblock, nofirmwareexec, source/parserpolicy; actualmanifestusespuremetadata')
