#!/usr/bin/env python3
"""Bounded deployed-payload ADT inventory; no MAC/unique ID/calibration values printed."""
from pathlib import Path
import hashlib,importlib.util,json,struct,sys
E=Path(__file__).resolve().parent
R=next(p for p in E.parents if(p/'AGENTS.md').exists())
sys.dont_write_bytecode=True
sys.path.insert(0,str(R/'tools'));import adt
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
stage=R/'research/nord2/wifi-ios-security/ios'
manifest=json.loads((stage/'manifest.json').read_text())
payload=(stage/'payload.bin').read_bytes()
assert hashlib.sha256(payload).hexdigest()=='07f369366eea4ad8ec15fb22b457a9408262fa806774bf6427707113bd76b258'
assert manifest['image_sha256']=='6e4ce860cfbd0b27fbace65e61e96edc8d2af8067603652cd67a0939c53331c6'
off,size=struct.unpack_from('<QQ',payload,0x20);assert off+size<=len(payload)
blob=payload[off:off+size];assert hashlib.sha256(blob).hexdigest()==manifest['private_calibration_adt_sha256']
root,end=adt.parse(blob);assert end==len(blob)
def flatten(root):
 out={}
 def visit(node,path):
  out[path]=node
  for child in node.children:visit(child,path.rstrip('/')+'/'+child.name)
 visit(root,'/');return out
nodes=flatten(root)
target_names={'model','compatible','target-type','target-sub-type','manufacturer',
 'board-id','chip-id','unique-chip-id','platform-name','serial-number','local-mac-address','mac-address-wifi0',
 'mac-address','nvram','nvram-total-size','nvram-bank-size','device_type','IOBuiltin',
 'display-scale','crypto-hash-method','boot-manifest-hash','nord2-wifi-calibration'}
safe_strings={'model','compatible','target-type','target-sub-type','manufacturer','device_type','crypto-hash-method','wifi-chipset','product-name','product-description'}
def props(node):
 out=[]
 for name,flags,val in node.props:
  row=dict(name=name,placeholder_flags=flags,bytes=len(val),registry_type='OSData (DT importer)')
  if name in safe_strings:row['public_text']=val.rstrip(b'\0').decode('ascii').split('\0')
  elif name in ['chip-id','display-scale'] and len(val)==4:row['u32_le']=hex(int.from_bytes(val,'little'))
  out.append(row)
 return out
selected={p:props(nodes[p]) if p in nodes else None for p in ['/','/chosen','/product','/arm-io','/arm-io/wlan','/platform','/options']}
presence={name:[dict(path=p,bytes=len(v),placeholder_flags=f) for p,node in nodes.items() for k,f,v in node.props if k==name] for name in sorted(target_names)}
topaz_path=R/'research/reference/ios-topaz-topaz/tools/mkadt.py'
spec=importlib.util.spec_from_file_location('upstream_topaz_adt',topaz_path);topaz=importlib.util.module_from_spec(spec);spec.loader.exec_module(topaz)
# Use the same filtered public D22 product for a useful schema comparison, never a different phone's placeholders.
topaz.d22ap_product=lambda:[(k,v) for k,f,v in nodes['/product'].props if k not in ['name','AAPL,phandle']]
up=flatten(topaz.build(seed=b'\x5a'*64))
up_selected={p:props(up[p]) if p in up else None for p in ['/','/chosen','/product','/arm-io','/arm-io/wlan','/platform','/options']}
assert not presence['mac-address-wifi0'] and not presence['local-mac-address']
assert nodes['/chosen'].prop('nord2-wifi-calibration')==(R/'research/nord2/wifi/calibration/WIFI').read_bytes()
assert nodes['/chosen'].prop('unique-chip-id') is not None and len(nodes['/chosen'].prop('unique-chip-id'))==8
result=dict(image_sha256=manifest['image_sha256'],payload_sha256=hashlib.sha256(payload).hexdigest(),
 adt_bytes=len(blob),adt_sha256=hashlib.sha256(blob).hexdigest(),
 actual_deployed_source='938cb89b827bf7cd477044714ed23b935d4d6bd0',
 scope='Exact verified image payload ADT before loader/runtime property updates; not a live registry dump.',
 selected_nodes=selected,target_property_presence=presence,
 upstream_topaz_schema_comparison=up_selected,
 upstream_build_note='Old pinned mkadt.py is unchanged in exact currentTopaz89451b8..e4772911 diff. Same public filtered product supplied in memory for schema comparison; no original reference mutation.',
 source_pins={str(p.relative_to(R)):sha(p) for p in [R/'tools/adt.py',R/'tools/mkadt.py',R/'tools/mkadt-nord2.py',topaz_path,stage/'manifest.json',R/'shim/nord2-xnu/loader.c']},
 private_values_not_output=True)
(E/'inventory.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(dict(adt_bytes=len(blob),adt_sha256=result['adt_sha256'],chosen_keys=[x['name'] for x in selected['/chosen']],wlan=selected['/arm-io/wlan'],missing_mac_properties=True),indent=2))
