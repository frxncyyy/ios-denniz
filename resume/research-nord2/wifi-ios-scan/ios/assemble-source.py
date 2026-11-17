#!/usr/bin/env python3
"""Stage reviewed private bridge sources under unique native names; no device/CI writes."""
from pathlib import Path
import re,json,hashlib
ROOT=Path(__file__).resolve().parents[4]
B=ROOT/'research/nord2/wifi/ios-service-bridge'
OUT=Path(__file__).resolve().parent/'integrated-source'
mapping={
 'control_contract':'wifi_ios_control',
 'bind-transport-policy/binding_contract':'wifi_ios_binding',
 'event-pipe/pipe':'wifi_ios_event',
 'lifecycle-scan-adapter/v2/lifecycle_scan':'wifi_ios_lifecycle',
 'card-capability-contract/v2/card':'wifi_ios_card',
 'completed-scan-adapter-age/age':'wifi_ios_age',
 'completed-scan-adapter/snapshot':'wifi_ios_snapshot',
 'initial-query-audit/cache_cursor':'wifi_ios_cursor',
 'scan-and-initialization/scan_contract':'wifi_ios_scan_contract',
 'passive-scan-contract/v2/mapping':'wifi_ios_mapping',
 'scan-backend-boundary/backend_bss':'wifi_ios_bss',
 'consumer-integration/niw_rate_compat':'wifi_ios_rates',
 'ios-controller-integration/wifi_ios_policy':'wifi_ios_policy',
}
paths={}
for source,target in mapping.items():
 for ext in ('.c','.h'):paths[(B/(source+ext)).resolve()]=target+ext
paths[(B/'ios-controller-integration/LifecycleShell.cpp').resolve()]='wifi_ios_client.cpp'
paths[(B/'ios-controller-integration/Registry.cpp').resolve()]='wifi_ios_registry.cpp'
aliases={'scan_bss.h':'wifi_scan_bss.h','wifi_scan_service.h':'wifi_scan_service.h','wifi_scan_plan.h':'wifi_scan_plan.h','wifi_scan_bss.h':'wifi_scan_bss.h'}
for source,target in paths.items():
 if source.suffix=='.h':aliases[source.name]=target
OUT.mkdir(exist_ok=False)
manifest={}
for source,target in paths.items():
 raw=source.read_text()
 def replace(m):
  name=m.group(1);resolved=(source.parent/name).resolve()
  if name in ('test-xnu.h','test-registry-xnu.h'):return m.group(0) # excluded in native compile
  if resolved in paths:replacement=paths[resolved]
  elif Path(name).name in aliases:replacement=aliases[Path(name).name]
  else:raise RuntimeError('unmapped include '+str(source)+': '+name)
  return '#include "../../../bsd/dev/'+replacement+'"' if source.suffix=='.cpp' else '#include "'+replacement+'"'
 code=re.sub(r'#include "([^"\n]+)"',replace,raw)
 (OUT/target).write_text(code)
 manifest[target]={'source':str(source.relative_to(ROOT)),'source_sha256':hashlib.sha256(raw.encode()).hexdigest(),'native_sha256':hashlib.sha256(code.encode()).hexdigest()}
(OUT/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print('Staged',len(manifest),'native bridge files; no canonical integration')
