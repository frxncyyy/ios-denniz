import struct,sys,bisect
from dsc import *
mm=mms[files[0]]
off,cnt=struct.unpack_from('<II',mm,0x1c0)
if cnt==0: off,cnt=struct.unpack_from('<II',mm,0x18)
imgs=[]
for i in range(cnt):
    a,_,_,po,_=struct.unpack_from('<QQQII',mm,off+32*i)
    imgs.append((a,mm[po:mm.find(b'\0',po)].decode()))
imgs.sort()
starts=[a for a,_ in imgs]
def which(x):
    i=bisect.bisect_right(starts,x)-1
    return imgs[i][1].split('/')[-1], x-imgs[i][0], imgs[i][0]
if __name__=='__main__':
    slide=int(sys.argv[1],16)
    for s in sys.argv[2:]:
        x=int(s,16)-slide
        if 0x180000000<=x<0x300000000:
            n,o,b=which(x); print(s,hex(x),n,'+'+hex(o))
        else: print(s,'(not cache)')
