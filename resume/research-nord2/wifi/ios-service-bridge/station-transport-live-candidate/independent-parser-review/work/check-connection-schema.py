#!/usr/bin/env python3
"""Read printf schemas only; does not execute the native code or any runner."""
from pathlib import Path
import hashlib,importlib.util,json,re
P=Path(__file__).resolve().parent;R=next(x for x in P.parents if(x/'AGENTS.md').exists())
s=importlib.util.spec_from_file_location('receipt',P/'parse-connection.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
files=[R/'shim/nord2-xnu/wifi_ios_client.cpp',R/'shim/nord2-xnu/wifi_ifnet_link.inc',R/'research/nord2/wifi/ios-service-bridge/wpa2-native-candidate/wifi_station_service_bind.inc']
found=set();records=[]
for p in files:
 text=p.read_text()
 for hit in re.finditer(r'printf\("NORD2 WIFI ([A-Z ]+): ([^"\n]*)\\n"',text):
  kind,fmt=hit.groups()
  if kind not in m.SCHEMAS:continue
  parts=fmt.split(' ');keys=[]
  for part in parts:
   key,value=part.split('=');keys.append(key)
   expected='%d'if(kind,key)in m.SIGNED else '%llu'if key in('generation','ticket','epoch','t_ms')else '%#x'if key in('native_flags','assoc_flags','transition_disable','addr','raw','mask','expected','status')and not(kind=='IOS STATION'and key=='status')else '%u'
   # The legacy scan event is the one intentional source literal.
   assert value==expected or(kind=='IOS EVENT'and((key=='event'and value=='10')or(key=='published'and value=='1'))),(kind,key,value,expected)
  assert keys in m.SCHEMAS[kind],(kind,keys)
  found.add((kind,tuple(keys)));records.append({'source':str(p.relative_to(R)),'line':text[:hit.start()].count('\n')+1,'kind':kind,'fields':keys})
assert found=={(k,tuple(v))for k,variants in m.SCHEMAS.items()for v in variants},found
report={'status':'PASS_PRINTF_SCHEMA_ONLY','schemas':len(found),'records':records,'source_sha256':{str(p.relative_to(R)):hashlib.sha256(p.read_bytes()).hexdigest()for p in files},'limits':'Static exact printf names/order/conversion widths only. Native private binder is not frozen yet. No execution/IPC/hardware/association evidence.'}
(P/'connection-schema-check.json').write_text(json.dumps(report,indent=2)+'\n');print(report['status'],len(found))
