#!/usr/bin/env python3
"""Offline stage tests. Never imports runner, builds images or contacts devices."""
from pathlib import Path
import ast,hashlib,importlib.util,json,sys
P=Path(__file__).resolve().parent;ROOT=P.parents[3]
s=importlib.util.spec_from_file_location('dma_verify',P/'verify-image.py');v=importlib.util.module_from_spec(s);s.loader.exec_module(v)
previous=json.loads((ROOT/'research/nord2/wifi-rom/ios/manifest.json').read_text())
assert v.COMMANDLINE==previous['commandline']+' nord2wifidma=1'
build=(P/'build-image.sh').read_text();assert v.COMMANDLINE in build
assert 'nord2-ios-wifi-dma-battery-stop23.img' in build
for f,key in [('identity-required.json','parent_verified'),('prepared-image-required.json','parent_verified'),('authorization-required.json','human_exact_image_authorized')]:assert json.loads((P/f).read_text())[key] is False
try:v.load_identity();assert False
except AssertionError:pass
runner=(P/'run-approved-boot.py').read_text();ast.parse(runner)
assert "['--authorized-wifi-dma-battery-boot']" in runner
assert "assert not (out/'hardware-live').exists()" in runner
assert "(out/'flash.log').open('x')" in runner
assert "(out/'client-hardware.log').open('x')" in runner
assert "run('flash', 'boot', str(img))" in runner and "run('erase'" not in runner
assert '33314816' in runner and '23a9bd30b7fc5d439bafcd10104932884a1a145afc2f5d34cd8dd6d463fd1b90' in runner
assert "installed['full_readback_verified']" in runner and '17179869184' in runner and 'device-readback.sha256' in runner
sys.path.insert(0,str(ROOT/'tools'));import adt
sp=importlib.util.spec_from_file_location('claimcheck',P/'rom_claim_static_check.py');c=importlib.util.module_from_spec(sp);sp.loader.exec_module(c)
r=adt.Node();ch=adt.Node();ch.props=[('name',0,b'chosen\0'),('nord2-conn-claim',0,bytes(24)),('nord2-wifi-dma-claim',0,bytes(24))];r.children=[ch];c.verify(adt.serialize(r))
ch.props[-1]=('nord2-wifi-dma-claim',0,b'\1'+bytes(23))
try:c.verify(adt.serialize(r));assert False
except AssertionError:pass
print('PASS stage exactbaseline+dmaarg/falsegates/oneusebootonlyrunnerstaticchecks/rootfsproof/dualstatic24zero claims; no build/runner/device')
