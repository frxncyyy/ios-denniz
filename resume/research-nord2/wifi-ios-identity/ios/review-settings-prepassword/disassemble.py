from pathlib import Path
import struct,runpy,subprocess,json
R=next(p for p in Path(__file__).resolve().parents if(p/'AGENTS.md').exists());D=Path(__file__).resolve().parent;B=R/'research/nord2/wifi/ios-service-bridge';C=runpy.run_path(str(B/'cache_view.py'))['Cache'](R/'research/nord2/ipsw/root-images/mounts/SystemOS/root/System/Library/Caches/com.apple.dyld');obj=R.parent/'toolchains/proton-clang/bin/llvm-objdump'
def dis(name,lo,hi):
 raw=C.read(lo,hi-lo);sect=struct.pack('<16s16sQQIIIIIIII',b'__text',b'__TEXT',lo,len(raw),184,2,0,0,0x80000400,0,0,0);seg=struct.pack('<II16sQQQQIIII',0x19,152,b'__TEXT',lo,len(raw),184,len(raw),7,5,1,0);h=struct.pack('<IIIIIIII',0xfeedfacf,0x100000c,0,1,1,152,0,0);p=D/(name+'.analysis.macho');p.write_bytes(h+seg+sect+raw)
 (D/(name+'.txt')).write_bytes(subprocess.check_output([str(obj),'-d',str(p)]))
for item in [('select-wrapper',0x1f57651a8,0x1f5765560),('scan-for-association',0x1f576f390,0x1f576f600),('details-join',0x1f5788ce8,0x1f5788d40)]:dis(*item)
x=json.loads((D/'methods.json').read_text())
for image in x:
 for c in image['classes']:
  if c['class_name']=='WFPasswordPromptOperation':print(json.dumps(c,indent=2))
def sel(a):
 raw=C.read(a,16);i,j=struct.unpack_from('<II',raw)
 if i&0x9f000000!=0x90000000:return {'raw':raw.hex()}
 imm=((i>>5)&0x7ffff)<<2|((i>>29)&3)
 if imm&(1<<20):imm-=1<<21
 page=(a&~0xfff)+(imm<<12);at=page+((j>>10)&4095)*8
 try:
  p=struct.unpack('<Q',C.read(at,8))[0]&0xffffffffff
  return {'ref':hex(at),'string':C.read(p,200).split(b'\0')[0].decode()}
 except Exception as e:return {'raw':raw.hex(),'error':str(e)}
resolved={hex(a):sel(a)for a in [0x1f580f0a0,0x1f5808660,0x1f5806ae0,0x1f580bd00,0x1f5805600,0x1f580bf80]}
resolved['block_flags']={'address':'0x1f57d6c08','raw_hex':C.read(0x1f57d6c08,8).hex(),'meaning':'Block literal flags/reserved words copied through d0; not a timeout or seconds value.'}
(D/'resolved.json').write_text(json.dumps(resolved,indent=2)+'\n');print(json.dumps(resolved,indent=2))
