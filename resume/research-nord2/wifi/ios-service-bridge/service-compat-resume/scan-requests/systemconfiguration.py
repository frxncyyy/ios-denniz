from pathlib import Path
import struct,sys,json,subprocess,hashlib,re
P=Path(__file__).resolve().parent;B=P.parents[1];R=next(p for p in P.parents if (p/'shim/nord2-xnu').is_dir());sys.path.insert(0,str(B));from cache_view import Cache
C=Cache(R/'research/nord2/ipsw/root-images/mounts/SystemOS/root/System/Library/Caches/com.apple.dyld');a=0x190638000;h=C.read(a,32);n,sz=struct.unpack_from('<II',h,16);cmds=C.read(a+32,sz);o=0
for _ in range(n):
 cmd,z=struct.unpack_from('<II',cmds,o)
 if cmd==25 and cmds[o+8:o+24].split(b'\0')[0]==b'__TEXT':lc=bytearray(cmds[o:o+z]);v,vs,fo,fs=struct.unpack_from('<QQQQ',lc,24);break
 o+=z
assert fs<2*1024*1024;original=C.read(v,fs);data=bytearray(original);struct.pack_into('<Q',lc,40,0)
for i in range(struct.unpack_from('<I',lc,64)[0]):
 off=72+80*i;x=struct.unpack_from('<I',lc,off+48)[0];struct.pack_into('<I',lc,off+48,x-fo if x else 0)
struct.pack_into('<I',data,12,1);struct.pack_into('<II',data,16,1,len(lc));data[32:32+len(lc)]=lc;f=P/'SystemConfiguration-TEXT-analysis.macho';f.write_bytes(data)
s=subprocess.check_output([str(R.parent/'toolchains/proton-clang/bin/llvm-objdump'),'-d',str(f)],text=True);(P/'SystemConfiguration-disassembly.txt').write_text(s)
meta={'base':hex(a),'original_TEXT_address':hex(v),'original_TEXT_bytes':fs,'original_TEXT_sha256':hashlib.sha256(original).hexdigest(),'carrier_not_loadable':True};(P/'SystemConfiguration-identity.json').write_text(json.dumps(meta,indent=2)+'\n');print(json.dumps(meta))
