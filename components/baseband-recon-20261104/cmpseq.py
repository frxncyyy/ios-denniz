import struct,re,sys
vend=[]
prev_more=False
for l in open('ccci_trace2.log',errors='ignore'):
    p=l.rstrip('\n').split(' ')
    if len(p)>6 and p[0]=='R' and p[2]=='/dev/ccci_fs' and p[6]:
        b=bytes.fromhex(p[6])
        d0=struct.unpack('<I',b[:4])[0] if len(b)>=4 else 0
        if prev_more:
            prev_more=bool(d0&0x80000000); continue
        prev_more=bool(d0&0x80000000)
        if len(b)<24: continue
        opid=struct.unpack('<I',b[16:20])[0]
        argc=struct.unpack('<I',b[20:24])[0]; off=24; args=[]
        for _ in range(argc):
            if off+4>len(b): break
            ln=struct.unpack('<I',b[off:off+4])[0]; off+=4; args.append(b[off:off+ln]); off+=(ln+3)&~3
        path=''
        if args and len(args[0])>=2 and len(args[0])%2==0 and args[0][1:2]==b'\0':
            try: path=args[0].decode('utf-16le').rstrip('\0')
            except: pass
        vend.append((opid,re.split(r'[\\/]',path)[-1] if path and path[1:2]==':' and len(path)>3 else ''))
mine=[]
for l in open('n2_run.out',errors='ignore'):
    m=re.match(r'fs ([0-9a-f]{4}) (\S*)',l)
    if m:
        pth=m.group(2); op=int(m.group(1),16)
        mine.append((op,pth.split('/')[-1] if pth.startswith('/') and pth.count('/')>4 else ''))
        if op==0x100e: mine[-1]=(op,'')
for i,(a,b) in enumerate(zip(vend,mine)):
    if a[0]!=b[0] or (a[1] and b[1] and a[1]!=b[1]):
        print('first divergence at',i)
        for j in range(max(0,i-6),i+6): print(j, vend[j] if j<len(vend) else None, mine[j] if j<len(mine) else None)
        break
else: print('prefix identical',min(len(vend),len(mine)))
