#!/usr/bin/env python3
"""Link the real Nord2 Metal frontend and both native compilers, no stubs.
No device access/installation. Undefined symbols are fatal, no dynamic_lookup.
"""
from pathlib import Path
import subprocess,plistlib,hashlib,json
r=Path(__file__).resolve().parents[1];sdk=r/'build/apple-toolchain/sdks/iPhoneOS16.5.sdk'
out=r/'build/native/Nord2Metal.bundle';out.mkdir(exist_ok=True)
frontend=r/'build/tegu-frontend/libNord2MetalFrontend.a'
air=r.parent/'research/nord2-native-air/target/aarch64-apple-ios/release/libtegu_air_bridge.a'
mali=r/'build/native/libNord2Mali.a'
subprocess.run(['clang','--target=arm64-apple-ios16.0','-isysroot',str(sdk),'-fuse-ld=lld','-bundle',
 '-Wl,-adhoc_codesign,-dead_strip','-Wl,-force_load,'+str(frontend),str(air),str(mali),
 '-framework','Metal','-framework','Foundation','-framework','Security','-lobjc','-lSystem','-lc++','-o',str(out/'Nord2Metal')],check=True)
info={'CFBundleIdentifier':'org.nord2.Metal','CFBundleExecutable':'Nord2Metal','CFBundleName':'Nord2Metal',
 'CFBundlePackageType':'BNDL','CFBundleVersion':'1','CFBundleShortVersionString':'0.1','CFBundleSupportedPlatforms':['iPhoneOS'],
 'MinimumOSVersion':'16.0','NSPrincipalClass':'TeguComputeDevice'}
(out/'Info.plist').write_bytes(plistlib.dumps(info,fmt=plistlib.FMT_BINARY))
# A pipeline pack is only valid for the build id linked into the executable;
# drop one left over from an earlier link (package_metal_addon.py --pipeline-pack re-adds the right one).
import importlib.util
spec=importlib.util.spec_from_file_location('mkpack',r/'metal/tegu/make_pipeline_pack.py');pk=importlib.util.module_from_spec(spec);spec.loader.exec_module(pk)
build_id=pk.bundle_build_id(out);pack=out/'PipelinePack.tpcpack'
if pack.exists() and pk.pack_tag(pack.read_bytes())!=pk.build_tag(build_id):
 pack.unlink();print('removed stale PipelinePack.tpcpack (built for another id)')
record={'pipeline_cache_build':build_id,'pipeline_pack':pack.exists(),'target':'arm64-apple-ios16.0','undefined_symbols_allowed':False,'device_load_tested':False,'system_registered':False,
 'inputs':{str(p.relative_to(r.parent)):hashlib.sha256(p.read_bytes()).hexdigest() for p in (frontend,air,mali)},
 'bundle_files':{p.name:{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in out.iterdir() if p.is_file()}}
(r/'build/native/bundle-receipt.json').write_text(json.dumps(record,indent=2)+'\n')
print(json.dumps(record,indent=2))
