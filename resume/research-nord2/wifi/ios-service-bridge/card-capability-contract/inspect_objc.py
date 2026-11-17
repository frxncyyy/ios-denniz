from pathlib import Path
import struct,sys,json
P=Path(__file__).resolve().parent;sys.path.insert(0,str(P.parent));from cache_view import Cache
R=P.parents[4];C=Cache(R/'research/nord2/ipsw/root-images/mounts/SystemOS/root/System/Library/Caches/com.apple.dyld');base=0x1abd5e000
ptr=lambda a:struct.unpack('<Q',C.read(a,8))[0]&0xffffffffff
s=lambda a:C.read(a,256).split(b'\0')[0].decode()
h=C.read(base,32);n,z=struct.unpack_from('<II',h,16);b=C.read(base+32,z);o=0;classes=[]
for _ in range(n):
 cmd,z=struct.unpack_from('<II',b,o)
 if cmd==25:
  ns=struct.unpack_from('<I',b,o+64)[0]
  for j in range(ns):
   q=o+72+80*j;name=b[q:q+16].rstrip(b'\0');a,sz=struct.unpack_from('<QQ',b,q+32)
   if name==b'__objc_classlist':classes=[ptr(a+k) for k in range(0,sz,8)]
 o+=z
out=[]
for cl in classes:
 try:
  ro=ptr(cl+32)&~7;name=s(ptr(ro+24))
  if name!='CWFApple80211':continue
  methods=[]
  for which,obj in [('instance',cl),('class',ptr(cl))]:
   rr=ptr(obj+32)&~7;ml=ptr(rr+32)
   if not ml:continue
   flags,count=struct.unpack('<II',C.read(ml,8));es=flags&0xfffc;relative=bool(flags&0x80000000)
   if count>2048:raise ValueError('count')
   for k in range(count):
    a=ml+8+k*es
    if relative:
     no,ty,im=struct.unpack('<iii',C.read(a,12));sel=(0x18229979f+no) if flags&0x40000000 else a+no
     if not flags&0x40000000:sel=ptr(sel)
     imp=a+8+im
    else:sel=ptr(a);imp=ptr(a+16)
    methods.append({'kind':which,'selector':s(sel),'imp':hex(imp)})
  out.append({'class':name,'address':hex(cl),'methods':[m for m in methods if m['selector']=='capabilities:']})
 except (ValueError,UnicodeDecodeError):continue
(P/'methods.json').write_text(json.dumps(out,indent=2)+'\n')
for cl in out:
 print(cl['class']);print('\n'.join(x['imp']+' '+x['selector'] for x in cl['methods']))
