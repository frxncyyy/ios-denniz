#!/usr/bin/env python3
"""Read-only closed-template checks; never invoke image builder/runner/device."""
from pathlib import Path
import ast,hashlib,importlib.util,json,subprocess,sys
sys.dont_write_bytecode=True
P=Path(__file__).resolve().parent
R=next(p for p in P.parents if (p/'AGENTS.md').is_file())
def read(n):return json.loads((P/n).read_text())
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
protected=[p for p in P.rglob('*') if p.is_file() and p.name not in ('closed-preflight-report.json','closed-preflight.stdout','closed-preflight.stderr','stage-template-manifest.json')]
before={str(p.relative_to(P)):sha(p) for p in protected}
identity=read('identity-required.json');schema=read('native-schema-required.json');prepared=read('prepared-image-required.json');approval=read('authorization-required.json');proposal=read('source-proposals.json')
assert identity['parent_verified'] is schema['parent_verified'] is prepared['parent_verified'] is False
assert schema['schema_pending'] is True and schema['live_control_reviewed'] is False and schema['bounded_rejoin_reviewed'] is False
assert all(identity[k] is None for k in ('source_commit','ci_run','ci_attempt','kernel_path','kernel_sha256','kernel_bytes','ci_run_json'))
assert all(prepared[k] is None for k in ('image_sha256','image_bytes','independent_image_review_path','independent_image_review_sha256'))
assert all(approval[k] is False for k in ('human_exact_image_authorized','rf_calibration_authorized','conditional_recovery_authorized'))
assert all(approval[k] is None for k in ('image_sha256','device_serial','authorization_source','authorization_answer','recorded_at'))
assert proposal['parent_verified'] is proposal['proposal_reviewed'] is False and isinstance(proposal['source_pins'],list) and proposal['source_pins']==schema['source_pins']
for n in ('RUNNER_EXECUTION_USED','manifest.json','payload.bin','image-build.log','flash.log','hardware-live','client-hardware.log','client-hardware.pid'):assert not (P/n).exists(),n
assert not (R/prepared['image_path']).exists()
readers=read('reader-dependencies.json')
for item in readers['pins']:
 p=R/item['path'] if item.get('canonical_read_only_dependency') else P/item['path'];assert sha(p)==item['sha256']
 if item.get('byte_identical'):assert p.read_bytes()==(R/item['origin']).read_bytes()
# Import the exact startup wrapper with its local core dependencies. No log or
# device operation occurs; dependency identity checks run at module import.
spec=importlib.util.spec_from_file_location('closed_startup_receipt',P/'parse-backend.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
for p in P.rglob('*.py'):ast.parse(p.read_text(),filename=str(p))
result=subprocess.run([sys.executable,'-B',str(P/'verify-image.py'),'--preflight'],cwd=R,capture_output=True,text=True)
(P/'closed-preflight.stdout').write_text(result.stdout);(P/'closed-preflight.stderr').write_text(result.stderr)
assert result.returncode!=0 and 'BLOCKED: parent has not supplied and verified CI identity' in result.stderr
assert before=={str(p.relative_to(P)):sha(p) for p in protected}
report={'state':'PASS_CLOSED_TEMPLATE_ONLY','identity_schema_prepared_human_RF_recovery_closed':True,'source_CI_kernel_image_payload_approval_identities_null':True,'runner_unused':True,'reader_dependencies_byte_identical_and_imported':True,'legacy_postjoin_unknown_families_filters_unchanged':True,'preflight_exit':result.returncode,'preflight_blocks_at_identity_before_work':True,'no_build_runner_CI_device_invoked':True,'stage_inputs_unchanged':True,'source_pins':before,'future_image_verified':False,'future_runtime_approved':False}
(P/'closed-preflight-report.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS closed/null gates, unchanged strict readers, local core dependency import and preflight refusal before work')
