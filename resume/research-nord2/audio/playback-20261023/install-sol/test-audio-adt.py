#!/usr/bin/env python3
import importlib.util,pathlib,sys,hashlib,json
HERE=pathlib.Path(__file__).resolve().parent;ROOT=HERE.parents[4];sys.path.insert(0,str(ROOT/'tools'));import adt
spec=importlib.util.spec_from_file_location('original',ROOT/'tools/mkadt-nord2.py');original=importlib.util.module_from_spec(spec);spec.loader.exec_module(original)
spec=importlib.util.spec_from_file_location('proposal',HERE/'mkadt-nord2-audio-proposal.py');proposal=importlib.util.module_from_spec(spec);spec.loader.exec_module(proposal)
source=str(ROOT/'research/nord2/ipsw/d22ap.adt')
base=adt.serialize(original.build(source,True));assert base==adt.serialize(proposal.build(source,True,False))
new=proposal.build(source,True,True)
products=[n for n in new.children if n.name=='product'];assert len(products)==1
assert len(products[0].children)==1;audio=products[0].children[0];assert audio.name=='audio'
assert set(k for k,f,v in audio.props)=={'name','AAPL,phandle','wireless-splitter','supports-always-listening'}
assert audio.prop('wireless-splitter')==audio.prop('supports-always-listening')==b'\1\0\0\0'
handles=[]
def walk(n):
 h=n.prop('AAPL,phandle')
 if h:handles.append(h)
 for c in n.children:walk(c)
walk(new);assert len(handles)==len(set(handles))
try:proposal.build(None,True,True)
except SystemExit:pass
else:raise AssertionError('audio allowed without product source')
print('PASS audio ADT opt-in default-byte-identity, exact native capabilities, unique phandles, required source')
(HERE/'adt-audio-comparison.json').write_text(json.dumps({'source':'research/nord2/ipsw/d22ap.adt','source_sha256':hashlib.sha256(pathlib.Path(source).read_bytes()).hexdigest(),'baseline_bytes':len(base),'baseline_sha256':hashlib.sha256(base).hexdigest(),'audio_bytes':len(adt.serialize(new)),'audio_sha256':hashlib.sha256(adt.serialize(new)).hexdigest(),'audio_capabilities':{'wireless-splitter':1,'supports-always-listening':1},'vdef_property_in_source':False},indent=2)+'\n')
