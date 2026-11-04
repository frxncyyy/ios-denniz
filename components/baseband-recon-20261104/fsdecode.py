import struct,collections,sys
def parse_args(b):
    # b = payload after op id : argc, then [len, data padded4]
    argc,=struct.unpack('<I',b[:4]); off=4; args=[]
    for _ in range(argc):
        if off+4>len(b): break
        l,=struct.unpack('<I',b[off:off+4]); off+=4
        d=b[off:off+l]; off+=(l+3)&~3; args.append(d)
    return args
def show(d):
    if len(d)==4: return 'u32=%d(0x%x)'%(struct.unpack('<i',d)[0],struct.unpack('<I',d)[0])
    if len(d)==8: return 'u64=%d'%struct.unpack('<Q',d)[0]
    try:
        if len(d)>=2 and len(d)%2==0 and all(d[i+1]==0 for i in range(1,min(len(d),20),2)):
            return 'wstr=%r'%d.decode('utf-16le').rstrip('\0')
    except Exception: pass
    return 'bytes[%d]=%s'%(len(d),d[:24].hex()+('..' if len(d)>24 else ''))
events=[]
for l in open('ccci_trace.log',errors='ignore'):
    p=l.rstrip('\n').split(' ')
    if len(p)<6: continue
    events.append((p[0],p[1],p[2],p[3],p[4],p[6] if len(p)>6 else ''))
byop=collections.defaultdict(list)
i=0
for idx,(op,tid,name,ln,ret,hx) in enumerate(events):
    if name=='/dev/ccci_fs' and op=='R' and hx and len(hx)>=40:
        b=bytes.fromhex(hx)
        opid=struct.unpack('<I',b[16:20])[0]
        # find reply: next W on same tid
        j=idx+1; inter=[]
        while j<len(events) and not (events[j][0]=='W' and events[j][2]=='/dev/ccci_fs' and events[j][1]==tid):
            if events[j][1]==tid: inter.append(events[j])
            j+=1
        rep=bytes.fromhex(events[j][5]) if j<len(events) and events[j][5] else b''
        byop[opid].append((b,rep,inter))
want=int(sys.argv[1],16) if len(sys.argv)>1 else None
n=int(sys.argv[2]) if len(sys.argv)>2 else 3
for opid in sorted(byop):
    if want is not None and opid!=want: continue
    print('=== op 0x%x  count %d'%(opid,len(byop[opid])))
    seen=0
    for b,rep,inter in byop[opid][:n]:
        a=parse_args(b[20:])
        print('  REQ hdr',b[:16].hex(),' args:',[show(x) for x in a])
        if rep:
            ra=parse_args(rep[20:]); print('  REP hdr',rep[:16].hex(),'op=%x'%struct.unpack('<I',rep[16:20])[0],' args:',[show(x) for x in ra])
        for e in inter[:6]: print('    sys',e[0],e[2],e[3],e[4],e[5][:60])
