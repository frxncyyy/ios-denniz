from pathlib import Path
import subprocess,shutil,os,re,sysconfig,json
base=Path(__file__).resolve().parent;root=base/'initramfs';root.mkdir(exist_ok=True)
repo=Path.cwd();tools=(repo/'../toolchains/rootfs-tools').resolve()
def copy(src,dst=None):
 src=Path(src);dst=root/str(dst or src).lstrip('/');dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst)
def binary(src,dst=None):
 copy(src,dst)
 p=subprocess.run(['ldd',str(src)],capture_output=True,text=True)
 for line in p.stdout.splitlines():
  for dep in re.findall(r'(/[^ ]+) \(',line):copy(dep)
copy(tools/'busybox/usr/bin/busybox','/bin/busybox')
for name in ('bash','cp','ln','mkdir','find','install','df','sha256sum','stat','tar'):
 binary(shutil.which(name),'/bin/'+name)
binary(shutil.which('python3'),'/usr/bin/python3')
stdlib=Path(sysconfig.get_path('stdlib'));target=root/str(stdlib).lstrip('/')
if not target.exists():shutil.copytree(stdlib,target,ignore=shutil.ignore_patterns('site-packages','__pycache__','test','tests','ensurepip','idlelib','tkinter','turtledemo','config-*'))
for f in (stdlib/'lib-dynload').glob('*.so'):
 p=subprocess.run(['ldd',str(f)],capture_output=True,text=True)
 for dep in re.findall(r'(/[^ ]+) \(',p.stdout):copy(dep)
for name in ('mkfs.hfsplus','fsck.hfsplus'):binary(tools/'hfsprogs/usr/sbin'/name,'/bin/'+name)
copy(tools/'hfsprogs/usr/share/misc/hfsbootdata','/usr/share/misc/hfsbootdata')
mods=[]
for module in ('hfsplus','9pnet_virtio','9p','nls_utf8'):
 for line in subprocess.check_output(['modprobe','--show-depends',module],text=True).splitlines():
  if line.startswith('insmod '):
   src=Path(line.split()[1]);dest=root/'modules'/src.name.removesuffix('.zst');dest.parent.mkdir(exist_ok=True)
   if dest.name not in mods:
    dest.write_bytes(subprocess.check_output(['zstd','-dc',str(src)]));mods.append(dest.name)
copy(repo/'tools/sweet-root-tweaks.py','/build/sweet-root-tweaks.py')
# Reuse the base-image setup from the existing repository builder.
original=(repo/'tools/mkiosroot.sh').read_text();body=original[original.index('# Shared cache where dyld'):original.index('df -h "$work/root"')];body=body.replace('sudo ','')
(root/'build/finish-base.sh').write_text('#!/bin/bash\nset -euo pipefail\nwork=/work\n'+body)
(base/'modules.json').write_text(json.dumps(mods))
print('Prepared initramfs with',len(mods),'modules')
