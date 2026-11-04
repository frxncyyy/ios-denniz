#!/usr/bin/env python3
"""Strict private link using preserved audio CI objects + GPU overlay.
No stubs, undefined symbols or device operations. Support objects are built by
build_kernel_support.py. Archive member names and untouched object digests are
checked before replacing the two integration objects.
"""
from pathlib import Path
import subprocess,json,shlex,hashlib,os,shutil
r=Path(__file__).resolve().parents[2];p=r/'gpu/build/kernel-native'
ci=Path(os.environ.get('NORD2_BASE_LINK',str(r.parents[3]/'research/ci-40034353275/xnu-kernel/kernel.development.sweet.link')))
# Candidate is nested under research/nord2/gpu/<night>/work.
if not ci.exists():ci=Path('$NORD2_REPO/research/ci-40034353275/xnu-kernel/kernel.development.sweet.link')
ct=Path('$HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/toolchains/cctools/bin/aarch64-apple-darwin-ld')
ar=os.environ.get('LLVM_AR','$HOME/Documents/Codex/2026-10-13/por/work/huawei/command-line-tools/sdk/default/openharmony/native/llvm/bin/llvm-ar')
base=ci/'kernel.development.sweet.a';archive=p/'kernel.nord2.a';shutil.copyfile(base,archive)
for name in ['KernelConfigTables.cpo','sweet_touch.o']:
 members=subprocess.check_output([ar,'t',str(archive)],text=True).splitlines()
 if members.count(name)!=1:raise SystemExit('ambiguous replacement '+name)
 subprocess.run([ar,'r',str(archive),str(p/name)],check=True)
flags=[]
for a in shlex.split((ci/'kernel.development.sweet.linkarguments').read_text()):
 if a.startswith('-Wl,'):flags+=a[4:].split(',')
flags=[x for x in flags if x!='-new_linker']
objects=[p/'firehose_buffer.o',p/'nord2_chkstk.o']+sorted((p/'compiler-rt').glob('*.o'))+sorted((r/'gpu/build/objs').glob('nord2_gpu_*.o'))+[r/'gpu/build/objs/Nord2GPU.o']
cmd=[str(ct),'-arch','arm64','-macos_version_min','14.0']+flags+['-dead_strip','-alias_list',str(ci/'kernel.development.sweet.alias.exp'),'-exported_symbols_list',str(ci/'kernel.development.sweet.exp'),'-all_load',str(archive)]+[str(x) for x in objects]+['-o',str(p/'kernel.development.nord2-metal')]
with (p/'candidate-link.log').open('w') as log:subprocess.run(cmd,stdout=log,stderr=log,check=True)
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
def members(path):
 result={}
 with path.open('rb') as f:
  assert f.read(8)==b'!<arch>\n'
  while header:=f.read(60):
   assert len(header)==60 and header[58:]==b'`\n';size=int(header[48:58]);name=header[:16].decode().strip();data=f.read(size)
   if size&1:f.read(1)
   if name.startswith('#1/'):
    n=int(name[3:]);name=data[:n].rstrip(b'\0').decode();data=data[n:]
   name=name.rstrip('/')
   if name.startswith('__.SYMDEF') or not name:continue
   assert name not in result, 'duplicate archive member '+name
   result[name]=hashlib.sha256(data).hexdigest()
 return result
before,after=members(base),members(archive)
assert before.keys()==after.keys()
changed={n for n in before if before[n]!=after[n]}
assert changed=={'KernelConfigTables.cpo','sweet_touch.o'},changed
receipt={'base_archive':sha(base),'output':sha(p/'kernel.development.nord2-metal'),'inputs':{str(x.relative_to(r)):sha(x) for x in objects},'replacement_objects':{n:sha(p/n) for n in ['KernelConfigTables.cpo','sweet_touch.o']},'untouched_archive_members_verified':len(before)-2,'strict_link':True,'device_tested':False}
(p/'kernel-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');(p/'candidate-link-command.json').write_text(json.dumps(cmd,indent=2)+'\n');print(json.dumps(receipt,indent=2))
