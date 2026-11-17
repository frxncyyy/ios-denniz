#!/usr/bin/env python3
"""Private separate-TU package: include/header guard renames only."""
from pathlib import Path
import hashlib,json
here=Path(__file__).resolve().parent;native=here.parent;core=native.parent
root=next(p for p in here.parents if (p/'AGENTS.md').is_file())
active=root/'research/ci-39158169212/buildtree/xnu/bsd/dev'
inputs={'wifi_persistent.c':core/'completion.c','wifi_persistent.h':core/'completion.h','wifi_persistent_adapter.c':native/'adapter.c','wifi_persistent_adapter.h':native/'adapter.h','wifi_persistent_bind.inc':native/'xnu-bind.candidate.inc','sweet_touch.persistent.candidate.c':native/'v2/sweet_touch.persistent.candidate.c'}
replacements={
 'wifi_persistent.c':{'"completion.h"':'"wifi_persistent.h"','"../common-transaction/common.h"':'"wifi_common.h"'},
 'wifi_persistent.h':{'PRIVATE_NORD2_COMPLETION_H':'NORD2_WIFI_PERSISTENT_H'},
 'wifi_persistent_adapter.c':{'"adapter.h"':'"wifi_persistent_adapter.h"','"../../common-transaction/common.h"':'"wifi_common.h"'},
 'wifi_persistent_adapter.h':{'"../completion.h"':'"wifi_persistent.h"','PRIVATE_NPC_ADAPTER_H':'NORD2_WIFI_PERSISTENT_ADAPTER_H'},
 'wifi_persistent_bind.inc':{'"adapter.h"':'"wifi_persistent_adapter.h"'},
 'sweet_touch.persistent.candidate.c':{f'"{active}/wifi_common_adapter.inc"':'"wifi_common_adapter.inc"',f'#include "{native}/xnu-bind.candidate.inc"':'#include "wifi_persistent_adapter.h"\n#include "wifi_persistent_bind.inc"'},
}
m={'status':'FROZEN-private-separate-TU-include-guard-only','build':'sweet_touch includes header+bind; compile wifi_persistent.c and wifi_persistent_adapter.c as separate BSD translation units','symbols':'all original npc_/npa_/statics preserved; no canonical public collision found','mapping':{},'external_dependencies':{}}
for output,src in inputs.items():
 old=src.read_text();new=old
 for a,b in replacements[output].items():assert a in new;(None);new=new.replace(a,b)
 (here/output).write_text(new)
 m['mapping'][output]={'input':str(src),'input_sha256':hashlib.sha256(old.encode()).hexdigest(),'output_sha256':hashlib.sha256(new.encode()).hexdigest(),'replacements':replacements[output],'symbol_renames':{}}
for n in ['wifi_common.c','wifi_common.h','wifi_common_adapter.inc']:
 f=root/'shim/nord2-xnu'/n;m['external_dependencies'][n]={'path':str(f),'sha256':hashlib.sha256(f.read_bytes()).hexdigest()}
(here/'mapping-manifest.json').write_text(json.dumps(m,indent=2)+'\n')
print('Prepared6 PRIVATE separate-TU copies: include/headerguard changes only; no symbol changes.')
