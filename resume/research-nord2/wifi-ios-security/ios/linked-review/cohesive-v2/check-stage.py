#!/usr/bin/env python3
"""Read-only closed-stage/pin/schema preflight; never promotes any gate."""
from pathlib import Path
import json,hashlib,runpy,re,argparse
P=Path(__file__).resolve().parent;O=P.parents[1];R=next(x for x in O.parents if(x/'AGENTS.md').exists());a=argparse.ArgumentParser();a.add_argument('--report',type=Path);args=a.parse_args();sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();checks=[]
def check(x,label):assert x,label;checks.append(label)
cfg=json.loads((O/'native-schema-required.json').read_text());check(cfg['parent_verified']is False and cfg['schema_pending']is True,'native/schema gateclosed')
identity=json.loads((O/'identity-required.json').read_text());check(identity['parent_verified']is False and identity['ci_run']is None and identity['kernel_sha256']is None,'CIidentitygateclosed')
prepared=json.loads((O/'prepared-image-required.json').read_text());check(prepared['parent_verified']is False and prepared['image_sha256']is None,'preparedimagegateclosed')
auth=json.loads((O/'authorization-required.json').read_text());check(not any(auth[x] for x in ('human_exact_image_authorized','rf_calibration_authorized','conditional_recovery_authorized'))and auth['image_sha256']is None,'exactauthorizationgatesclosed')
for item in cfg['connection_diagnostics']:check(sha(R/item['path'])==item['sha256'],'diagnostic pin '+item['path'])
for marker in ['NORD2 WIFI IOS CARD:','mode=%u auth_lower=%u auth_upper=%u rsn_bytes=%u','rx_raw=%u rx_raw_eapol=%u rx_raw_drop=%u']:check(marker in cfg['required_new_diagnostic_markers'],'requiredmarker '+marker)
ns=runpy.run_path(str(O/'parse-connection.py'))
for name,prefix,kind in [('wifi_ios_client.cpp','NORD2 WIFI IOS JOINREQ:','IOS JOINREQ'),('wifi_station_service_bind.inc','NORD2 WIFI IOS STATION:','IOS STATION')]:
 lines=(R/'shim/nord2-xnu'/name).read_text().splitlines();line=next(x for x in lines if'printf("'+prefix in x);fmt=line.split('printf("',1)[1].split('\\n"',1)[0];fields=re.findall(r'([a-z_]+)=%',fmt);check(fields in ns['SCHEMAS'][kind],'exact canonical emitter '+kind)
for rel,wanted in json.loads((P/'source-pins.json').read_text())['files'].items():check(sha(R/rel)==wanted,'linkedsourcepin '+rel)
check(len(cfg['required_new_diagnostic_markers'])==len(set(cfg['required_new_diagnostic_markers'])),'diagnostic markers unique')
# Unknown candidate CI/sourcebundle is deliberately blocking; no finalbuildschema
# or parentverified flag is created here.
check(cfg['linked_reviews']==[],'actual linkedverdict stillpending')
report=dict(status='PASS_CLOSED_STAGE_LOCAL_PINS',checks=checks,source_schema_pending=True,CI_identity_pending=True,linked_verdict_pending=True,image_prepared=False,hardware_authorized=False,device_actions=False)
if args.report:args.report.write_text(json.dumps(report,indent=2)+'\n')
print('PASS',len(checks),'closed-stage source/schema checks; no execution gates changed')
