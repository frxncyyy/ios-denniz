#!/usr/bin/env python3
"""Independent existing-image verifier. Offline only; no build/download/device actions.
Rebuilds unchanged loader and strips CI kernel inside a fresh private outputdir.
"""
import argparse,hashlib,importlib.util,json,pathlib,re,struct,subprocess,sys
sys.dont_write_bytecode=True
P=pathlib.Path(__file__).resolve().parent
R=next(p for p in P.parents if(p/'AGENTS.md').is_file())
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
v=load('deep_image_helpers',R/'research/nord2/wifi-ios-scan-cancel/ios/verify-image.py')
need,sha,align=v.need,v.digest,v.align
BASESHA='ed05ea02d0499fe4109e6498a6852e769c346226aa4a56a220338673f8a2f5bf'
BOARD='635300fbc96f258ff9a35f82435f2e9d3ff6f1370c156067b40146e011038692'
def main():
 ap=argparse.ArgumentParser(description=__doc__)
 for n in ('stage','image','payload','kernel','plugin-artifact','output'):ap.add_argument('--'+n,type=pathlib.Path,required=True)
 ap.add_argument('--mode',choices=('playback','rollback'),required=True)
 ap.add_argument('--ci-run',type=int,default=39969463181);ap.add_argument('--source',default='111c77af36ab491a37146804080bebfe2e99b7e5');ap.add_argument('--attempt',type=int,default=1)
 a=ap.parse_args();need(not sys.flags.optimize,'no Python-O');need(re.fullmatch('[0-9a-f]{40}',a.source),'fullsourceSHA')
 stage=a.stage.resolve();baseline_path=stage/'baseline.adt'
 if not baseline_path.exists():baseline_path=R/'research/nord2/wifi-ios-scan-cancel/ios/baseline.adt'
 out=a.output.resolve();need(out.is_relative_to(P) and not out.exists(),'freshprivateoutputchild required')
 result=stage/'build-result.json';need(result.is_file(),'build-result missing; waitforparent')
 result_json=json.loads(result.read_text())
 need((result_json['source_commit'],result_json['ci_run'],result_json['ci_attempt'])==(a.source,a.ci_run,a.attempt),'build-resultCIidentity')
 need(result_json['authorized'] is False and result_json['hardware_tested'] is False,'offlineauthorization/hardwareclaims')
 entries=[i for i in result_json['images'] if i['role']==a.mode];need(len(entries)==1,'solemodeentry');entry=entries[0]
 need((R/entry['image']).resolve()==a.image.resolve() and (R/entry['payload']).resolve()==a.payload.resolve(),'exactmodepaths')
 ci=R/f'research/ci-{a.ci_run}';run=json.loads((ci/'run.json').read_text())
 need((run['status'],run['conclusion'],run['headSha'],run['databaseId'],run['attempt'])==('completed','success',a.source,a.ci_run,a.attempt),'CIidentity')
 files=[a.image,a.payload,a.kernel,result,ci/'run.json',a.kernel.parent/'SHA256SUMS',baseline_path,R/'research/nord2/rescue/recompressed/base.img',R/'backup/nord2/boot.img',a.plugin_artifact/'provenance.json',a.plugin_artifact/'audio.pkg',*list((a.plugin_artifact/'RedwoodAudio.driver').glob('*')),*list((a.plugin_artifact/'source').glob('*')),*[R/p for p in v.REBUILD_SOURCES]]
 before={str(p.resolve()):v.file_sha(p) for p in files}
 need(before[str((R/'research/nord2/rescue/recompressed/base.img').resolve())]==BASESHA,'basepin')
 need(any(line.split()[0]==v.file_sha(a.kernel) and pathlib.Path(line.split()[-1].lstrip('*')).name==a.kernel.name for line in(a.kernel.parent/'SHA256SUMS').read_text().splitlines() if len(line.split())==2),'CIkernelchecksum')
 for f in v.REBUILD_SOURCES:need(sha(subprocess.check_output(['git','show',a.source+':'+f],cwd=R))==v.file_sha(R/f),'sourcepinnedloaderinput '+f)
 img=a.image.read_bytes();payload=a.payload.read_bytes();need(len(img)<=33554432,'32MiBcapacity')
 need((entry['image_sha256'],entry['image_bytes'],entry['payload_sha256'],entry['boot_spare_bytes'])==(sha(img),len(img),sha(payload),33554432-len(img)),'exactmodeartifactidentity')
 need(result_json['kernel_sha256']==v.file_sha(a.kernel),'build-resultkernelidentity')
 # Require exact image/payload/kernel hashes occur as structured values in build result.
 def leaves(o):
  if isinstance(o,dict):return [x for z in o.values() for x in leaves(z)]
  if isinstance(o,list):return [x for z in o for x in leaves(z)]
  return [o]
 evidence=leaves(result_json)
 for pin in (sha(img),sha(payload),v.file_sha(a.kernel),a.source,a.ci_run):need(pin in evidence,'missingbuild-resultidentity '+str(pin))
 (kg,rd,dt),offsets,sizes,end=v.parts(img);need(end==len(img),'imageend')
 base=(R/'research/nord2/rescue/recompressed/base.img').read_bytes();stock=(R/'backup/nord2/boot.img').read_bytes()
 (bkg,brd,bdt),_,_,_=v.parts(base);(skg,_,sdt),_,_,_=v.parts(stock)
 blob,app=v.gunzip(kg);linux,stockapp=v.gunzip(skg);blinux,bapp=v.gunzip(bkg)
 need(linux==blinux and app==bapp==stockapp,'stockLinux/appendedDTS');need(rd==brd and dt==bdt==sdt,'rescueRamdisk/headerDTB')
 linuxsize=struct.unpack_from('<Q',linux,16)[0];need(linux[56:60]==b'ARM\x64' and len(linux)<=linuxsize<=0x7000000,'Linuxshape')
 poff=align(0x200000+linuxsize,0x4000);need(blob[0x200000:0x200000+len(linux)]==linux and not any(blob[0x200000+len(linux):poff]) and blob[poff:]==payload,'packedLinux/payload')
 need(0x40080000+len(blob)<=0x51100000 and len(blob)<=0x6200000,'loadRAMbounds')
 for off,size in zip(offsets,sizes):need(not any(img[off+size:off+align(size,2048)]),'padding')
 h=hashlib.sha1()
 for c in(kg,rd,b'',b'',dt):h.update(c);h.update(struct.pack('<I',len(c)))
 need(img[576:608]==h.digest().ljust(32,b'\0'),'headerID')
 allowed=set(range(8,12))|set(range(1636,1644))|set(range(576,608));need(all(x==y or i in allowed for i,(x,y)in enumerate(zip(img[:2048],base[:2048]))),'headerchanges');need(struct.unpack_from('<Q',img,1636)[0]==0,'DTBOoffset')
 adt=load('image_review_adt',R/'tools/adt.py');adtblob=v.payload_adt(payload);baseline=baseline_path.read_bytes();need(sha(baseline)==result_json['baseline_adt_sha256'],'build-resultbaselineADT');need(sha(adtblob)==entry['adt_sha256'],'build-resultmodeADT');need(baseline==(R/'research/nord2/wifi-ios-scan-cancel/ios/baseline.adt').read_bytes(),'baselineADTpin')
 tree,used=adt.parse(adtblob);need(used==len(adtblob) and adt.serialize(tree)==adtblob,'ADTroundtrip')
 chosen=[n for n in tree.children if n.name=='chosen'];need(len(chosen)==1,'solechosen')
 props=chosen[0].props;packages=[(f,b)for k,f,b in props if k=='nord2-audio-package'];boards=[(f,b)for k,f,b in props if k=='nord2-audio-board']
 if a.mode=='playback':
  need(len(packages)==len(boards)==1 and packages[0][0]==boards[0][0]==0,'audioADTproperties');need(len(boards[0][1])==4163 and sha(boards[0][1])==BOARD,'boardpin')
  pkg=packages[0][1];need(pkg==(a.plugin_artifact/'audio.pkg').read_bytes(),'CIpackagebytes')
  need(len(pkg)>=96,'packagesize');magic,version,total,count,na,nb,res,ha,hb=struct.unpack('<4sIIIII8s32s32s',pkg[:96]);need((magic,version,total,count,res)==(b'N2AP',1,len(pkg),2,b'\0'*8) and na+nb==len(pkg)-96,'N2APheader')
  binary,plist=pkg[96:96+na],pkg[96+na:];need(hashlib.sha256(binary).digest()==ha and hashlib.sha256(plist).digest()==hb,'N2APdigests');prov=json.loads((a.plugin_artifact/'provenance.json').read_text());need(prov['commit']=='a79ff0772a3454fd1ef127e181e5b3bce6c9ce18','HALsourcepin')
  builder=load('image_review_hal_builder',R/'tools/nord2-build-audio-plugin.py');need(prov['sources']==builder.FILES,'HALsourcehashmetadata')
  for n,pin in builder.FILES.items():need(v.file_sha(a.plugin_artifact/'source'/n)==pin,'HALsourcebytes '+n)
  for n,b in [('RedwoodAudio.driver/RedwoodAudio',binary),('RedwoodAudio.driver/Info.plist',plist),('audio.pkg',pkg)]:need(prov['outputs'][n]==sha(b) and (a.plugin_artifact/n).read_bytes()==b,'HALprovenance '+n)
  chosen[0].props=[p for p in props if p[0]not in('nord2-audio-package','nord2-audio-board')];need(adt.serialize(tree)==baseline,'onlyaudioADTdelta')
 else:need(not packages and not boards and adtblob==baseline,'rollbackbaselineADT')
 old=(R/'research/nord2/wifi-ios-scan-cancel/ios/payload.bin').read_bytes();basecmd=old[0x48:0x48+608].split(b'\0',1)[0];need(len(basecmd)==555,'baselinecmd555')
 expected=basecmd+(b' nord2audio=1 nord2audioinstall=1' if a.mode=='playback' else b' nord2audioinstall=2');need(payload[0x48:0x48+608]==expected.ljust(608,b'\0') and entry['commandline']==expected.decode('ascii'),'exactcommandline')
 out.mkdir(parents=True);v.command(['sh',R/'tools/nord2-build-loader.sh',out/'loader'],out/'loader.log');loader=(out/'loader/loader-Image').read_bytes();need(blob[:0x200000]==loader.ljust(0x200000,b'\0'),'loaderrebuild')
 strip=(R/'../toolchains/cctools/bin/aarch64-apple-darwin-strip').resolve();v.command([strip,'-S','-x',a.kernel,'-o',out/'kernel.strip'],out/'strip.log');fixed=bytearray((out/'kernel.strip').read_bytes());mk=load('image_review_macho',R/'tools/mkpayload.py');m=mk.parse_macho(fixed);fixups=mk.apply_chained_fixups(fixed,m);segs=[s for s in m['segs']if s['vmsize']];need(struct.unpack_from('<II',payload,8)==(1,len(segs)),'segmentcount');cursor=0x2a8+32*len(segs)
 for i,s in enumerate(segs):
  need(struct.unpack_from('<4Q',payload,0x2a8+32*i)==(s['vmaddr'],s['vmsize'],cursor,s['filesize']),'segmentextent');need(payload[cursor:cursor+s['filesize']]==fixed[s['fileoff']:s['fileoff']+s['filesize']],'exactrebasedkernel');cursor+=s['filesize'];need(not any(payload[cursor:align(cursor,16)]),'segmentpad');cursor=align(cursor,16)
 ao,az=struct.unpack_from('<QQ',payload,0x20);need(cursor==ao and az==len(adtblob) and len(payload)==align(ao+az,16) and not any(payload[ao+az:]),'ADTlayout')
 phys,mem=mk.adt_dram(adtblob);kend=align(max(s['vmaddr']+s['vmsize']for s in segs),0x4000);va=m['segs'][0]['vmaddr']&~(0x2000000-1);need(struct.unpack_from('<7Q',payload,0x10)==(va,m['entry'],ao,az,kend,phys,mem),'payloadaddresses');need((phys,mem)==(0x142000000,0x3e000000),'DRAMwindow')
 objdump=(R/'../toolchains/proton-clang/bin/llvm-objdump').resolve()
 v.command([objdump,'--disassemble','--no-show-raw-insn','--mattr=+rcpc,+rcpc-immo',a.kernel],out/'kernel-disassembly.txt')
 dis=(out/'kernel-disassembly.txt').read_text();instructions=0
 unknown_instructions=dis.count('<unknown>')
 for line in dis.splitlines():
  match=re.match(r'^\s*(?:[0-9a-fA-F]+:)\s+([a-zA-Z][a-zA-Z0-9.]*)\s',line)
  if match:
   instructions+=1;need(not re.match(r'^(?:ldapur|stlur)',match.group(1),re.I),'unsupportedv8.4unscaledacquire/release '+line)
 need(instructions>100000,'instructiondecodecoverage')
 need(before=={str(p.resolve()):v.file_sha(p)for p in files},'inputchanged')
 report=dict(verdict='PASS',mode=a.mode,source=a.source,ci_run=a.ci_run,image_sha256=sha(img),image_bytes=len(img),boot_spare_bytes=33554432-len(img),payload_sha256=sha(payload),kernel_sha256=v.file_sha(a.kernel),adt_sha256=sha(adtblob),fixups=fixups,inputs=before,hardware_tested=False,unknown_instructions=unknown_instructions,decoded_instructions=instructions,unsupported_ldapur_stlur=False,limitations=['TargetedISA scan rejects LDAPUR/STLUR; not a complete ISA compatibility proof. No hardware success inferred.'])
 (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':
 try:main()
 except(Exception)as e:print('FAILED: '+str(e),file=sys.stderr);sys.exit(1)
