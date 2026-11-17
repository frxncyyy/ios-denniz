from pathlib import Path
import sys,struct,hashlib,json
P=Path(__file__).resolve().parent;R=next(x for x in P.parents if (x/'shim/nord2-xnu').is_dir());B=P.parents[1];sys.path.insert(0,str(B));from cache_view import Cache
C=Cache(R/'research/nord2/ipsw/root-images/mounts/SystemOS/root/System/Library/Caches/com.apple.dyld');W=(R/'research/nord2/wifi/ios-abi/wifid').read_bytes();assert hashlib.sha256(W).hexdigest()=='0ec6ad6ee120fb107510393c485a19ceaf54ef8dc7da479899a8cf7b93cdf871';assert hashlib.sha256(C.read(0x190638000,512000)).hexdigest()=='6c66f212500375389c7d781e01460e2931e21dcd0a9d2f89ad472260d870b168';assert hashlib.sha256(C.read(0x1d3eb9000,344064)).hexdigest()=='c1e1afa676d75de03fd7bee970ccf8cbf63d6199e862a48ebed41aeea0df3aea'
segs=[];o=32
for _ in range(struct.unpack_from('<I',W,16)[0]):
 cmd,z=struct.unpack_from('<II',W,o)
 if cmd==25:v,vs,f,fs=struct.unpack_from('<QQQQ',W,o+24);segs.append((v,f,fs))
 o+=z
def rd(a,n):
 hits=[(v,f)for v,f,z in segs if v<=a and a-v+n<=z];assert len(hits)==1;v,f=hits[0];return W[f+a-v:f+a-v+n]
verified=0;files={}
for p in sorted(P.glob('*.txt')):
 if p.name=='SystemConfiguration-disassembly.txt':continue
 count=0
 for line in p.read_text().splitlines():
  try:a=int(line.split(':',1)[0],16)
  except ValueError:continue
  q=line.split(':',1)[1].split();assert len(q)>=4;raw=bytes(int(x,16)for x in q[:4]);assert raw==(rd(a,4)if 0x100000000<=a<0x101000000 else C.read(a,4)),(p,a);count+=1
 verified+=count;files[p.name]=count
for req,target in ((51,0x1d3ec57a8),(103,0x1d3ec6328),(106,0x1d3ec6394)):
 assert 0x1d3ec4038+4*struct.unpack('<H',C.read(0x1d3ef565e+2*(req+1),2))[0]==target
log=R/'research/nord2/wifi-ios-scan/ios/backend-full.log';d=log.read_bytes();assert len(d)==375276 and hashlib.sha256(d).hexdigest()=='555765f9ba8a1d46ccb788b7c2c13cddab968b7b1bf9faa0535f8ce8b5aa3563';assert b'IOS ENQUEUE:'not in d and b'request=10 operation='not in d
print(json.dumps({'result':'PASS','bounded_original_instructions':verified,'files':files,'actual_log_identity':True,'no_SET10_or_enqueue':True}))
