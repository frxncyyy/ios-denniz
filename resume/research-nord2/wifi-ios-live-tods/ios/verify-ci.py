"""Check exact CI metadata, artifact checksum and every generated patch target."""
from pathlib import Path
import hashlib,json,re,subprocess,tarfile
R=next(p for p in Path(__file__).resolve().parents if (p/'AGENTS.md').is_file())
P=Path(__file__).resolve().parent
C=R/'research/ci-39932615713'
source='a92be30891637cc8be7b435e3d67cebcd8a3dafd'
r=json.loads((C/'run.json').read_text())
assert (r['databaseId'],r['headSha'],r['status'],r['conclusion'])==(39932615713,source,'completed','success')
subprocess.run(['git','merge-base','--is-ancestor',source,'HEAD'],cwd=R,check=True)
subprocess.run(['git','diff','--exit-code',source,'--','shim','tools','xnu','.github'],cwd=R,check=True)
k=C/'xnu-kernel/kernel.development.sweet';h=hashlib.sha256(k.read_bytes()).hexdigest()
expected=[l.split()[0] for l in (k.parent/'SHA256SUMS').read_text().splitlines() if len(l.split())==2 and Path(l.split()[1].lstrip('*')).name==k.name]
assert expected==[h]
patch=(R/'xnu/patches/0082-nord2-early-probe.patch').read_text()
targets=set(re.findall(r'^\+\+\+ b/([^\n]+)$',patch,re.M));found={}
with tarfile.open(C/'xnu-buildtree/buildtree.tgz','r:gz') as tar:
 for m in tar:
  if m.name.startswith('xnu/') and m.name[4:] in targets:
   rel=m.name[4:];assert m.isfile() and m.size<32*1024*1024 and rel not in found
   data=tar.extractfile(m).read();q=R/'research/ci-39158169212/buildtree/xnu'/rel
   assert data==q.read_bytes(),rel
   found[rel]=hashlib.sha256(data).hexdigest()
assert set(found)==targets,(len(found),len(targets),targets-set(found))
log=(C/'build.log').read_text(errors='replace');commands={}
for n in ('wifi_station_wire','wifi_station_tx','wifi_station_transport','wifi_live_control','sweet_touch'):
 lines=[l for l in log.splitlines() if ' -c ' in l and re.search(r'/'+re.escape(n)+r'\.c(?:\s|$)', l)]
 assert lines,n
 assert all('-mcpu=generic' in l and '-march=armv8.2-a+crypto' in l for l in lines),n
 commands[n]=len(lines)
report=dict(ci_run=39932615713,source_commit=source,kernel_sha256=h,kernel_bytes=k.stat().st_size,patch_targets_equal=found,compile_commands_generic_armv82=commands,hardware_tested=False,dhcp_proven=False)
(P/'ci-verification.json').open('x').write(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:report[k] for k in ('ci_run','source_commit','kernel_sha256','kernel_bytes')}))
print('Generated patch targets verified:',len(found))
