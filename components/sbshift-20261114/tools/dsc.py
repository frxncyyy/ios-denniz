import struct, mmap, os, glob, sys
D='$HOME/dsc-work/sys/System/Library/Caches/com.apple.dyld/'
files=[D+'dyld_shared_cache_arm64']+sorted(glob.glob(D+'dyld_shared_cache_arm64.[0-9][0-9]'))+[D+'dyld_shared_cache_arm64.27.dylddata',D+'dyld_shared_cache_arm64.28.dyldlinkedit']
maps=[]  # (addr,size,mm,fileoff,name)
mms={}
for f in files:
    if not os.path.exists(f) or os.path.getsize(f)==0: print("skip",f,file=sys.stderr); continue
    fd=open(f,'rb'); mm=mmap.mmap(fd.fileno(),0,access=mmap.ACCESS_READ); mms[f]=mm
    if mm[:4]!=b'dyld': continue
    mo,mc=struct.unpack_from('<II',mm,0x10)
    for i in range(mc):
        a,s,o,_,_=struct.unpack_from('<QQQII',mm,mo+32*i)
        maps.append((a,s,mm,o,os.path.basename(f)))
def v2f(a):
    for b,s,mm,o,n in maps:
        if b<=a<b+s: return mm,o+(a-b),n
    return None
def read(a,n):
    r=v2f(a); 
    if not r: return None
    mm,o,_=r; return mm[o:o+n]
def cstr(a):
    r=v2f(a)
    if not r: return None
    mm,o,_=r; e=mm.find(b'\0',o,o+512); return mm[o:e].decode(errors='replace')
def f2v(fname,off):
    for b,s,mm,o,n in maps:
        if n==fname and o<=off<o+s: return b+off-o
def findstr(s):
    out=[]
    pat=b'\0'+s.encode()+b'\0'
    for b,sz,mm,o,n in maps:
        i=mm.find(pat,o,o+sz)
        while i!=-1:
            out.append(b+i+1-o); i=mm.find(pat,i+1,o+sz)
    return out
if __name__=='__main__':
    for m in maps: print(hex(m[0]),hex(m[1]),m[4],hex(m[3]))
