from pathlib import Path
import hashlib,json,re,tarfile
S=Path(__file__).resolve().parent.parent;R=next(x for x in S.parents if(x/'AGENTS.md').is_file())
identity=json.loads((S/'identity-required.json').read_text());assert identity['parent_verified'] is True,'CI identity pending'
P=(R/identity['kernel_path']).parent.parent
sha=lambda b:hashlib.sha256(b).hexdigest()
run=json.loads((P/'run.json').read_text());assert run['status']=='completed' and run['conclusion']=='success' and run['headSha']==identity['source_commit'] and run['databaseId']==identity['ci_run'] and run['attempt']==identity['ci_attempt']
k=P/'xnu-kernel/kernel.development.sweet';digest=sha(k.read_bytes())
sums=(k.parent/'SHA256SUMS').read_text().splitlines();assert [line.split()[0] for line in sums if line.split()[-1].lstrip('*')==k.name]==[digest]
names={'xnu/'+n for n in re.findall(r'^\+\+\+ b/(.*)$',(R/'xnu/patches/0082-nord2-early-probe.patch').read_text(),re.M)};names.add('xnu/iokit/Kernel/arm/SweetKeyStore.cpp');files=[]
assert {'xnu/bsd/dev/wifi_live_control.c','xnu/bsd/dev/wifi_live_control.h','xnu/bsd/dev/wifi_live_control_bind.inc','xnu/bsd/dev/wifi_station_rejoin.h'}<=names,'Missing live/rejoin generator expected sources'
with tarfile.open(P/'xnu-buildtree/buildtree.tgz','r|gz') as t:
 for member in t:
  if member.name not in names:continue
  b=t.extractfile(member).read();expected=(R/'research/nord2/wifi/claude-opus-security-failure/aks-candidate/proposed/iokit/Kernel/arm/SweetKeyStore.cpp') if member.name=='xnu/iokit/Kernel/arm/SweetKeyStore.cpp' else (R/'research/ci-39158169212/buildtree'/member.name);assert b==expected.read_bytes(),member.name
  files.append({'member':member.name,'sha256':sha(b)});names.remove(member.name)
assert not names,names
(P/'source-buildtree-audit.json').write_text(json.dumps({'passed':True,'files':files},indent=2)+'\n')
log=(P/'build.log').read_text();audit={}
units=[(n.stem,n.suffix) for n in (R/'shim/nord2-xnu').glob('wifi_ios_*') if n.suffix in ('.c','.cpp')]+[(n,'.c') for n in ('sweet_touch','wifi_normal_transport','wifi_normal_rx','wifi_vcn33','wifi_scan_bss')]
units += [(n,'.c') for n in ('wifi_station_diag','wifi_station_wire','wifi_station_tx','wifi_station_transport','wifi_station_transaction','wifi_wpa_crypto','wifi_wpa2','wifi_wpa_owner','wifi_wpa_nst','wifi_wpa_diag','wifi_station_data_diag','wifi_live_control')]
units += [('IOStartIOKit','.cpp'),('IOService','.cpp'),('SweetKeyStore','.cpp')]
for name,ext in units:
 lines=[x for x in log.splitlines() if '/'+name+ext in x and ' -c ' in x and '-mcpu=' in x]
 assert lines and all('-mcpu=generic' in x and '-march=armv8.2-a+crypto' in x for x in lines),name
 audit[name]={'compile_lines':len(lines),'generic_all':True}
(P/'normal-arm64-audit.json').write_text(json.dumps(audit,indent=2)+'\n')
print(json.dumps({'kernel_sha256':digest,'kernel_bytes':k.stat().st_size,'buildtree_files_equal':len(files),'actual_compile_units':len(audit)}))
