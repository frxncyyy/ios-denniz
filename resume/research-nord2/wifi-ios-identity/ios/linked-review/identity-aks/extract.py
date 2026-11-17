from pathlib import Path
import json,hashlib,runpy,re
R=next(p for p in Path(__file__).resolve().parents if (p/'shim/nord2-xnu').is_dir());P=Path(__file__).parent;CI=R/'research/ci-39804982005'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
r=json.loads((CI/'run.json').read_text());assert r['status']=='completed' and r['conclusion']=='success' and r['headSha']=='6b3302d1e039ca451b02a8aa2d8773aea269d8e4'
k=CI/'xnu-kernel/kernel.development.sweet';sums=(k.parent/'SHA256SUMS').read_text().splitlines();expected=next(l.split()[0] for l in sums if l.split()[-1].lstrip('*')==k.name);assert sha(k)==expected
M=runpy.run_path(str(R/'research/nord2/wifi-ios-profile/ios/linked-review/review.py'))['MachO'];m=M(k)
functions=[('registry_start','IO80211Controller::start('),('aks_dispatch','AppleKeyStoreUserClient::externalMethod('),('aks_wipe','sweet_aks_compat_wipe(')]
records=[]
for label,pattern in functions:
 a,e,s=m.bounds(pattern,False);body=m.read(a,e-a);(P/(label+'.bin')).write_bytes(body);text=m.dump(a,e);(P/(label+'.s')).write_text(text);records.append({'name':label,'symbol':s,'address':hex(a),'end':hex(e),'bytes':len(body),'sha256':hashlib.sha256(body).hexdigest(),'disassembly_sha256':sha(P/(label+'.s'))})
# Root's exact generated-source audit is reused for precisely these two units.
audit=json.loads((CI/'source-buildtree-audit.json').read_text());assert audit['passed'];files={x['member']:x['sha256'] for x in audit['files']}
registry=R/'shim/nord2-xnu/wifi_ios_registry.cpp';aks=R/'research/nord2/wifi/claude-opus-security-failure/aks-candidate/proposed/iokit/Kernel/arm/SweetKeyStore.cpp'
assert files['xnu/iokit/Kernel/arm/wifi_ios_registry.cpp']==hashlib.sha256(b'#include <arm64/sweet_early.h>\n#if defined(NORD2)\n'+registry.read_bytes()+b'\n#endif /* NORD2 */\n').hexdigest()
assert files['xnu/iokit/Kernel/arm/SweetKeyStore.cpp']==sha(aks)
(P/'extraction.json').write_text(json.dumps({'ci':39804982005,'source':r['headSha'],'kernel_sha256':sha(k),'kernel_bytes':k.stat().st_size,'functions':records,'reviewed_sources':{str(registry.relative_to(R)):sha(registry),str(aks.relative_to(R)):sha(aks)},'two_generated_sources_equal_root_audit':True,'verdict':'EXTRACTED_PENDING_MANUAL_REVIEW'},indent=2)+'\n');print(json.dumps({'kernel':sha(k),'bodies':len(records)}))
