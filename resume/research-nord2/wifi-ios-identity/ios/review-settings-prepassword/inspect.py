from pathlib import Path
import struct,sys,json,re,hashlib,runpy
R=next(p for p in Path(__file__).resolve().parents if(p/'AGENTS.md').exists());D=Path(__file__).resolve().parent;B=R/'research/nord2/wifi/ios-service-bridge';C=runpy.run_path(str(B/'cache_view.py'))['Cache'](R/'research/nord2/ipsw/root-images/mounts/SystemOS/root/System/Library/Caches/com.apple.dyld')
ptr=lambda a:struct.unpack('<Q',C.read(a,8))[0]&0xffffffffff
s=lambda a:C.read(a,512).split(b'\0')[0].decode()
images=[(a,n)for a,n in C.images()if any(n.endswith('/'+x)for x in('WiFiKit','WiFiKitUI','MobileWiFi'))];out=[]
for base,image in images:
 n,z=struct.unpack_from('<II',C.read(base,32),16);b=C.read(base+32,z);o=0;classes=[];segs=[]
 for _ in range(n):
  cmd,z=struct.unpack_from('<II',b,o)
  if cmd==25:
   seg,va,vs,fo,fs=struct.unpack_from('<16sQQQQ',b,o+8)
   if seg.rstrip(b'\0')==b'__TEXT':segs.append(dict(start=hex(va),bytes=fs,sha256=hashlib.sha256(C.read(va,fs)).hexdigest()))
   ns=struct.unpack_from('<I',b,o+64)[0]
   for j in range(ns):
    q=o+72+80*j;name=b[q:q+16].rstrip(b'\0');a,sz=struct.unpack_from('<QQ',b,q+32)
    if name==b'__objc_classlist':classes=[ptr(a+k)for k in range(0,sz,8)]
  o+=z
 records=[]
 for cl in classes:
  try:
   ro=ptr(cl+32)&~7;name=s(ptr(ro+24))
   if not re.search(r'Password|NetworkList|Airport|Join|NetworkSettings|Details|Security',name):continue
   methods=[]
   for kind,obj in [('instance',cl),('class',ptr(cl))]:
    rr=ptr(obj+32)&~7;ml=ptr(rr+32)
    if not ml:continue
    flags,count=struct.unpack('<II',C.read(ml,8));es=flags&0xfffc;relative=bool(flags&0x80000000)
    if count>2048:raise ValueError('count')
    for k in range(count):
     a=ml+8+k*es
     if relative:
      no,ty,im=struct.unpack('<iii',C.read(a,12));sel=(0x18229979f+no)if flags&0x40000000 else ptr(a+no);imp=a+8+im
     else:sel=ptr(a);imp=ptr(a+16)
     methods.append(dict(kind=kind,selector=s(sel),imp=hex(imp)))
   records.append(dict(class_name=name,address=hex(cl),methods=methods))
  except(ValueError,UnicodeDecodeError):continue
 out.append(dict(image=image,base=hex(base),text=segs,classes=records))
(D/'methods.json').write_text(json.dumps(out,indent=2)+'\n')
for image in out:
 print(image['image'],len(image['classes']))
 for c in image['classes']:
  print(c['class_name']);print('\n'.join(x['imp']+' '+x['selector']for x in c['methods']if re.search(r'join|Join|password|Password|select|Select|connect|Connect|details|Details|security|Security|scan|Scan|cache|Cache|wait|Wait',x['selector'])))
