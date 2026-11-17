from pathlib import Path
import ast,json,subprocess
b=Path(__file__).resolve().parent;root=b.parents[2]
def values(path):
 tree=ast.parse(path.read_text());return {n.targets[0].id:ast.literal_eval(n.value) for n in tree.body if isinstance(n,ast.Assign) and isinstance(n.targets[0],ast.Name) and isinstance(n.value,ast.Constant)}
a=values(b/'diagnostic/verify-image.py');z=values(b/'ios-profile/verify-image.py')
old=json.loads((root/'research/nord2/ufs-two-lane/hardware-fixed/manifest.json').read_text())
assert a['COMMANDLINE']==old['commandline']
old=json.loads((root/'research/nord2/zoom-wifi/manifest.json').read_text())
expected=' '.join('nord2wifiemi=0' if t=='nord2wifiemi=1' else t for t in old['commandline'].split() if not t.startswith('nord2zoomreset='))
assert z['COMMANDLINE']==expected
for directory,v in [('diagnostic',a),('ios-profile',z)]:
 s=(b/directory/'verify-image.py').read_text();build=(b/directory/'build-image.sh').read_text()
 assert v['SOURCE']=='870271596eb0a09bd5b2920981681bf8cca200fc' and v['COMMANDLINE'] in build
 assert not any(t.split('=')[0]=='nord2zoomreset' for t in v['COMMANDLINE'].split())
 assert 'parents[4]' in s and 'SHA256SUMS' in s and 'segments_byte_verified=True' in s
 subprocess.run(['sh','-n',str(b/directory/'build-image.sh')],check=True)
 assert not (b/directory/'manifest.json').exists()
print('PASS prepared templates: exact priorRO bundle; normalRWargs minuszoom andEMIoff; source/CI/hash/segmentverification gates; no finalmanifests/imagesbuilt')
