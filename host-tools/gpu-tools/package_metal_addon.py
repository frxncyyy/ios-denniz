#!/usr/bin/env python3
"""Assemble a private verified native addon; never touches a phone/rootfs."""
from pathlib import Path
import gzip,hashlib,io,json,tarfile,importlib.util,argparse
r=Path(__file__).resolve().parents[1];native=r/'build/native'
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--output',type=Path,default=r/'build/device-candidate');ap.add_argument('--pipeline-pack',type=Path,help='pipeline pack made by metal/tegu/make_pipeline_pack.py for this exact bundle build id; shipped as Nord2Metal.bundle/PipelinePack.tpcpack, where the frontend looks for it')
ap.add_argument('--pipeline-corpus',type=Path,default=r/'build/pipeline-corpus/corpus.txt',help='accumulated pipeline descriptions (metal/tegu/pipeline_corpus.py scan); without --pipeline-pack a pack for this bundle is replayed from it')
ap.add_argument('--no-auto-pack',action='store_true',help='ship no pack even when a corpus exists')
ap.add_argument('--ca-prefs',action='store_true',help='also ship ca-preferences.json (disable CA deep/compressed framebuffers)')
ap.add_argument('--ab-schedule',type=Path,help='in-boot A/B toggle schedule (e.g. tools/ab_schedule_default.conf), shipped as Nord2Metal.bundle/ABSchedule.conf where the frontend reads it at init');args=ap.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
if (out/'nord2-metal-addon.tar.gz').exists():raise SystemExit('refuse to replace an existing addon archive')
# Pipeline pack: cache entries are keyed by the build id compiled into the
# bundle, so a pack for any other build can never be served.  Refuse to ship
# one (stale packs left in build/native by an earlier link included).
spec=importlib.util.spec_from_file_location('mkpack',r/'metal/tegu/make_pipeline_pack.py');pk=importlib.util.module_from_spec(spec);spec.loader.exec_module(pk)
bundle=native/'Nord2Metal.bundle';packed=bundle/'PipelinePack.tpcpack';build_id=pk.bundle_build_id(bundle)
if not args.pipeline_pack and not args.no_auto_pack and args.pipeline_corpus.exists() and any(l.startswith('TPC_MISS') for l in args.pipeline_corpus.read_text().splitlines()):
 # Every package carries a pack for its own build id covering every pipeline the corpus has seen (heavy: one replay).
 import subprocess,sys
 work=r/('build/pipeline-pack-'+build_id[12:20]);work.mkdir(exist_ok=True)
 subprocess.run([sys.executable,str(r/'tools/build_pipeline_pack.py'),'--bundle',str(bundle),'--corpus',str(args.pipeline_corpus),'--work',str(work)],check=True)
 args.pipeline_pack=work/'PipelinePack.tpcpack'
elif not args.pipeline_pack and not args.no_auto_pack:print('WARNING: no pipeline pack and no corpus: the first boot compiles every pipeline (seconds each); see metal/README.nord2.md')
if args.pipeline_pack:
 data=args.pipeline_pack.read_bytes()
 if pk.pack_tag(data)!=pk.build_tag(build_id):raise SystemExit('pipeline pack was made for another build than '+build_id[:24])
 pk.read_pack(data,pk.build_tag(build_id));packed.write_bytes(data)
elif packed.exists() and pk.pack_tag(packed.read_bytes())!=pk.build_tag(build_id):
 raise SystemExit('stale '+str(packed)+' (other build than '+build_id[:24]+'): remove it or pass --pipeline-pack')
# A/B schedule: only when asked for; never leave one from an earlier package.
schedule=bundle/'ABSchedule.conf'
if args.ab_schedule:
 text=args.ab_schedule.read_text()
 if not any(l.split()[:1]==['phase'] for l in text.splitlines()):raise SystemExit('A/B schedule has no phase line')
 schedule.write_text(text)
elif schedule.exists():schedule.unlink()
files={}
for name in ('Nord2Metal.bundle','nord2-metal-probe','varying.bc'):
 p=native/name
 for f in sorted(p.rglob('*')) if p.is_dir() else [p]:
  if f.is_file():files[str(f.relative_to(native))]=hashlib.sha256(f.read_bytes()).hexdigest()
manifest={'version':1,'files':files,'hardware_executed':False,'private_donor':True,
 'pipeline_cache_build':build_id,'pipeline_pack':files.get('Nord2Metal.bundle/PipelinePack.tpcpack'),
 'ab_schedule':files.get('Nord2Metal.bundle/ABSchedule.conf')}
(native/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
spec=importlib.util.spec_from_file_location('installer',r/'tools/stage_metal_root.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);m.validate_payload(native)
archive=out/'nord2-metal-addon.tar.gz'
with archive.open('wb') as raw,gzip.GzipFile(filename='',fileobj=raw,mode='wb',mtime=0) as gz,tarfile.open(fileobj=gz,mode='w') as t:
 for name in [*sorted(files),'manifest.json']:
  p=native/name;data=p.read_bytes();info=tarfile.TarInfo('payload/'+name);info.size=len(data);info.uid=info.gid=0;info.mtime=0
  info.mode=0o755 if p.name in ('Nord2Metal','nord2-metal-probe') else 0o644;t.addfile(info,io.BytesIO(data))
 if args.ca_prefs:
  # ios-tegu ROOT111/R74: with these three CoreAnimation booleans false,
  # backboardd allocates plain BGRA display surfaces instead of b3a8.
  prefs={'version':1,'keys':{'CAEnableDeepFramebuffer':False,'CAEnableFramebufferCompression':False,'CAEnableIOSurfaceCompression':False},
   'files':[{'path':'private/var/mobile/Library/Preferences/com.apple.backboardd.plist','uid':501,'gid':501,'mode':0o600},
            {'path':'private/var/mobile/Library/Preferences/.GlobalPreferences.plist','uid':501,'gid':501,'mode':0o600},
            {'path':'private/var/root/Library/Preferences/com.apple.backboardd.plist','uid':0,'gid':0,'mode':0o600},
            {'path':'private/var/root/Library/Preferences/.GlobalPreferences.plist','uid':0,'gid':0,'mode':0o600}]}
  data=(json.dumps(prefs,indent=2,sort_keys=True)+'\n').encode();info=tarfile.TarInfo('ca-preferences.json');info.size=len(data);info.mode=0o644;info.uid=info.gid=info.mtime=0;t.addfile(info,io.BytesIO(data))
 for p in (r/'tools/stage_metal_root.py',r/'tools/update_metal_root.py',r/'docs/METAL_DEVICE_PLAN.md'):
  data=p.read_bytes();info=tarfile.TarInfo(p.name);info.size=len(data);info.mode=0o644;info.uid=info.gid=info.mtime=0;t.addfile(info,io.BytesIO(data))
receipt={'path':str(archive),'sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),'bytes':archive.stat().st_size,'payload':manifest}
(out/'addon-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(receipt,indent=2))
