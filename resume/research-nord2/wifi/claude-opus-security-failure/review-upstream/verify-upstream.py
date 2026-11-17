#!/usr/bin/env python3
"""Check saved GitHub source objects and bounded immutable failure correlation."""
import hashlib
import json
from pathlib import Path
import re

E=Path(__file__).resolve().parent
R=next(p for p in E.parents if (p/'AGENTS.md').exists())
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def blob(p):
    b=p.read_bytes()
    return hashlib.sha1(b'blob '+str(len(b)).encode()+b'\0'+b).hexdigest()
def read(p):return json.loads(p.read_text())
refs={'MakrSas-ios-topaz':('topaz','e4772911e116ab4ab6c4ac1f38e6106fefe8b483'),
      'oosexgang-hackios-xnu-kernel':('main','8e61187098f2c26c5daee8f9f72b6416aa64b9ab'),
      'oosexgang-hackios-xnu-userland':('main','906425b75e02b4f99eb5d7bb1ef7c9583472d772')}
for n,(branch,commit) in refs.items():
    d=read(E/(n+'-branch.json'));assert d['name']==branch and d['commit']['sha']==commit
compare=read(E/'topaz-compare.json')
assert compare['base_commit']['sha']=='89451b811a4e6cc5cdcf734c45887163d92c599e'
assert compare['commits'][-1]['sha']==refs['MakrSas-ios-topaz'][1] and compare['total_commits']==4
changes={x['filename']:x for x in compare['files']}
objects=[]
for p in (E/'topaz-current').iterdir():
    matches=[(n,d) for n,d in changes.items() if Path(n).name==p.name]
    assert len(matches)==1
    n,d=matches[0];assert blob(p)==d['sha']
    objects.append(dict(repo='MakrSas/ios-topaz',commit=refs['MakrSas-ios-topaz'][1],path=n,git_blob=blob(p),sha256=sha(p)))
mapping={'hackios-xnu-kernel':{'MiniAppleKeyStore.cpp':'xnu/files/iokit/Kernel/MiniAppleKeyStore.cpp','kernel-LICENSE':'LICENSE'},
         'hackios-xnu-userland':{'inject.c':'userland/src/inject.c','wifipw_test.c':'userland/src/wifipw_test.c',
                               'rc.jobs.wifipw':'userland/rc.jobs.wifipw','com.redwood.wifi.plist':'userland/com.redwood.wifi.plist',
                               'net_test.m':'userland/src/net_test.m','LICENSE':'LICENSE'}}
for repo,files in mapping.items():
    tree=read(E/(repo+'-tree.json'));commit=refs['oosexgang-'+repo][1]
    assert tree['sha']==commit and tree['truncated'] is False
    lookup={x['path']:x for x in tree['tree'] if x['type']=='blob'}
    for local,remote in files.items():
        p=E/'oosexgang-current'/local;obj=lookup[remote]
        assert p.stat().st_size==obj['size'] and blob(p)==obj['sha']
        objects.append(dict(repo='oosexgang/'+repo,commit=commit,path=remote,git_blob=blob(p),sha256=sha(p)))
identical=[]
for filename,remote in [('redwood_apple80211.c','xnu/files/bsd/dev/redwood_apple80211.c'),
                        ('RedwoodWLAN.cpp','xnu/files/iokit/Kernel/RedwoodWLAN.cpp')]:
    p=E/'extracted'/filename;old=R/'research/reference/oosexgang/hackios-xnu-kernel'/remote
    assert p.read_bytes()==old.read_bytes()
    identical.append(dict(file=filename,git_blob=blob(p),sha256=sha(p),exact_old_reference_match=True))
local=R/'xnu/patches/0038-sweet-applekeystore.patch'
text=local.read_text();assert 'kAKSWrapKey          = 10' in text and 'kAKSUnwrapKey        = 11' in text
assert not re.search(r'case kAKS(?:Unwrap|Wrap)Key',text)
assert not re.search(r'case kAKS(?:Unwrap|Wrap)Key',(R/'xnu/patches/0055-sweet-aks-ext-state-sampler-depth.patch').read_text())
upstream=(E/'oosexgang-current/MiniAppleKeyStore.cpp').read_text()
assert 'case kAKSWrapKey:' in upstream and 'case kAKSUnwrapKey:' in upstream
trace=E.parent/'attempt-final.log';assert trace.stat().st_size==434903
assert sha(trace)=='3624e56ea8b63bbc7a7684824fde28227218b4b753e6a7e56104adb6103f0226'
selected=[]
for n,line in enumerate(trace.read_text().splitlines(),1):
    if 'SWEET AKS: securityd sel 10 ' in line:
        assert 'in 2/32 out 1/40 -> 0xe00002c7' in line
        selected.append(dict(line=n,text=line))
assert selected and any(x['line']<4440 for x in selected) and any(x['line']>4440 for x in selected)
apple=(E/'extracted/redwood_apple80211.c').read_text()
for case in [51,96,97]:assert not re.search(r'case\s+'+str(case)+r'\s*:',apple)
assert 'return kIOReturnUnsupported;' in (E/'extracted/RedwoodWLAN.cpp').read_text()
assert not re.search('wifi|80211|keychain|securityd',(E/'oosexgang-current/inject.c').read_text(),re.I)
out=dict(verdict='SOURCE_OBJECTS_AND_BOUNDED_CORRELATION_VERIFIED',references=refs,
         fresh_object_checks=objects,extracted_comparisons=identical,
         upstream_get51_get96_get97_absent=True,upstream_wifi_iouc_deliberately_unsupported=True,
         local_wrap_unwrap_declarations_without_handlers=True,
         actual_wrap_failures=len(selected),failures_before_first_late_setup=sum(x['line']<4440 for x in selected),
         failures_after_first_late_setup=sum(x['line']>4440 for x in selected),wrap_failure_rows=selected,
         trace=dict(path=str(trace.relative_to(R)),bytes=trace.stat().st_size,sha256=sha(trace)),
         source_pins={str(local.relative_to(R)):sha(local),
                      'xnu/patches/0055-sweet-aks-ext-state-sampler-depth.patch':sha(R/'xnu/patches/0055-sweet-aks-ext-state-sampler-depth.patch')},
         no_causal_crash_claim=True,no_target_code_executed=True,no_canonical_reference_or_device_mutation=True)
with (E/'verification.json').open('x') as f:f.write(json.dumps(out,indent=2)+'\n')
files={str(p.relative_to(E)):sha(p) for p in sorted(E.rglob('*')) if p.is_file() and p.name!='manifest.json'}
with (E/'manifest.json').open('x') as f:f.write(json.dumps(dict(files=files),indent=2)+'\n')
print(json.dumps(dict(verification_sha256=sha(E/'verification.json'),report_sha256=sha(E/'REPORT.md'),manifest_sha256=sha(E/'manifest.json'),objects=len(objects),wrap_failures=len(selected),before=sum(x['line']<4440 for x in selected),after=sum(x['line']>4440 for x in selected)),indent=2))
