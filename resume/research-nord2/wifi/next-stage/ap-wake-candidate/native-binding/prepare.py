from pathlib import Path
import json,hashlib
p=Path(__file__).resolve().parent;src=p.parent
rules={
 'wifi_wake.c':('wake.c',{'"wake.h"':'"wifi_wake.h"'}),
 'wifi_wake.h':('wake.h',{'PRIVATE_AP_WAKE_H':'NORD2_WIFI_WAKE_H'}),
 'wifi_wake_adapter.c':('adapter.c',{'"adapter.h"':'"wifi_wake_adapter.h"'}),
 'wifi_wake_adapter.h':('adapter.h',{'"wake.h"':'"wifi_wake.h"','PRIVATE_AP_WAKE_ADAPTER_H':'NORD2_WIFI_WAKE_ADAPTER_H'}),
}
m={}
for out,(name,repl) in rules.items():
 old=(src/name).read_text();new=old
 for a,b in repl.items():assert a in new;new=new.replace(a,b)
 (p/out).write_text(new);m[out]={'source':str(src/name),'source_sha256':hashlib.sha256(old.encode()).hexdigest(),'output_sha256':hashlib.sha256(new.encode()).hexdigest(),'renames':repl,'symbol_changes':False}
(p/'name-mapping.json').write_text(json.dumps(m,indent=2)+'\n')
