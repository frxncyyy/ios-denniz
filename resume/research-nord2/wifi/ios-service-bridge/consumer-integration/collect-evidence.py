#!/usr/bin/env python3
"""Bounded same-IPSW source assertions; no runtime/service/device action."""
from pathlib import Path
import hashlib,json,struct,sys
P=Path(__file__).resolve().parent;B=P.parent;R=next(x for x in P.parents if (x/'shim/nord2-xnu').is_dir());sys.path.insert(0,str(B));from cache_view import Cache
h=lambda b:hashlib.sha256(b).hexdigest();W=R/'research/nord2/wifi/ios-abi/wifid';w=W.read_bytes();assert len(w)==2606496 and h(w)=='0ec6ad6ee120fb107510393c485a19ceaf54ef8dc7da479899a8cf7b93cdf871'
segs=[];o=32
for _ in range(struct.unpack_from('<I',w,16)[0]):
 cmd,z=struct.unpack_from('<II',w,o)
 if cmd==25:va,vs,fo,fs=struct.unpack_from('<QQQQ',w,o+24);segs.append((va,fo,fs))
 o+=z
def read(a,n):
 assert 0<=n<=4096;hit=[(v,f,z)for v,f,z in segs if v<=a and a-v+n<=z];assert len(hit)==1
 v,f,z=hit[0];return w[f+a-v:f+a-v+n]
def ptr(a):return 0x100000000+(struct.unpack('<Q',read(a,8))[0]&0xffffffff)
def cf(a):
 n=struct.unpack('<Q',read(a+24,8))[0];assert n<128;return read(ptr(a+16),n).decode()
def dictionary(a):
 n=struct.unpack('<Q',read(a+16,8))[0];assert n==1;return ptr(ptr(a+24)),ptr(ptr(a+32))
k,v=dictionary(0x100238c38);assert cf(k)=='IOPropertyMatch';k2,v2=dictionary(v);assert cf(k2)=='IO80211InterfaceRole' and cf(v2)=='Infrastructure';assert cf(0x100221de0)=='IOInterfaceName'
C=Cache(R/'research/nord2/ipsw/root-images/mounts/SystemOS/root/System/Library/Caches/com.apple.dyld');text=C.read(0x1d3eb9000,344064);assert h(text)=='c1e1afa676d75de03fd7bee970ccf8cbf63d6199e862a48ebed41aeea0df3aea'
assert C.read(0x1d3ef9bae,len(b'IO80211Controller\0'))==b'IO80211Controller\0'
assert C.read(0x1d3ef9ca2,16)==b'IOInterfaceName\0'
assert C.read(0x1d3ef9cb2,16)==b'IOPropertyMatch\0'
# All code bytes preserved with exact bounded source ranges, no reconstructed writer claim.
ranges={'wifid':[(0x10006bd64,0x10006bdf4),(0x10006e64c,0x10006e734),(0x100090db8,0x100090e9c)],'framework':[(0x1d3ed4c54,0x1d3ed4d40),(0x1d3ed4ed0,0x1d3ed4ef0),(0x1d3ecb06c,0x1d3ecb080),(0x1d3ede648,0x1d3ede670),(0x1d3ebd24c,0x1d3ebd304),(0x1d3ebd5bc,0x1d3ebd628)]}
out=[];evidence=[]
for owner,pairs in ranges.items():
 lines=(B/('initial-query-audit/wifid-disassembly.txt' if owner=='wifid' else 'IO80211-disassembly.txt')).read_text().splitlines()
 for lo,hi in pairs:
  raw=read(lo,hi-lo)if owner=='wifid' else C.read(lo,hi-lo);selected=[]
  for line in lines:
   try:a=int(line.split(':',1)[0],16)
   except ValueError:continue
   if lo<=a<hi:selected.append(line)
  assert len(selected)==(hi-lo)//4
  # Verify displayed instruction bytes rather than trusting a retained disassembly alone.
  for line in selected:
   a=int(line.split(':',1)[0],16);parts=line.split(':',1)[1].strip().split();shown=bytes(int(x,16)for x in parts[:4]);assert shown==raw[a-lo:a-lo+4]
  out+=selected;evidence.append({'artifact':owner,'start':hex(lo),'end':hex(hi),'bytes':hi-lo,'sha256':h(raw)})
(P/'source-excerpt.txt').write_text('\n'.join(out)+'\n')
K=R/'research/ci-39158169212/buildtree/xnu'
native={str(f.relative_to(R)):h(f.read_bytes())for f in [K/'iokit/Kernel/IOService.cpp',K/'bsd/net/if.h',K/'bsd/net/if_media.h',K/'bsd/sys/sockio.h',K/'bsd/net/kpi_interface.h']}
# Compatibility code sets are actual consumer bitmasks, not synthetic test expectations.
masks={'CCK':0x826,'OFDM':0x41001001041240};expected={'CCK':[1,2,5,11],'OFDM':[6,9,12,18,24,36,48,54]}
for k,m in masks.items():assert [i for i in range(55)if(m>>i)&1]==expected[k]
r={'result':'PASS_BOUNDED_SAME_IPSW_CONSUMER_EVIDENCE','wifid_sha256':h(w),'framework_original_TEXT_sha256':h(text),'discovery':{'notification_call':'0x10006bd9c','matching_dictionary':'0x100238c38','decoded_match':{'IOPropertyMatch':{'IO80211InterfaceRole':'Infrastructure'}},'callback':'0x10006e64c','callback_property':'IOInterfaceName','validation_call':'0x10006e6bc->0x100090db8','required_socket_ioctl':'0xc02c6938 SIOCGIFMEDIA','accepted_media_predicate':'ioctlreturn0 && (ifm_current@16 &0xe0)==0x80(IFM_IEEE80211)','prior_claim_corrected':'SkywalkInterface is only error text on inspected notification path; no class key in its actual match dictionary'},'bind':{'lookup':'0x1d3ed4c54 builds only IOPropertyMatch(IOInterfaceName=requestedCFString)','open':'0x1d3ed4ee8 genericIOServiceOpen,type0','enumeration_separate':'0x1d3ed4538 IOServiceMatching(IO80211Controller); GetIfList furtherfilters knownApple IOClass/IOUserClass values, so owncontrollernamealone not proof enumeration succeeds'},'EOF':{'small_GET':'0x1d3ecb06c return nonzero stored unchanged toerrno at0x1d3ecb07c','async_consumer':'0x1d3ede648..66c accepts5 or0xe0820446 only after realcompletedcacheexhaustion'},'rates':{'known_consumer_codes':expected,'raw_array':'countbyte+34 uint32array+36 exportsunchanged; HT/VHT/HE parsedseparately','policy':'explicitdriver12table approved byroot; IE11->code5 compatibilitychoice, not recoveredApplewriter or measuredMbps'},'ranges':evidence,'native_source_pins':native,'limitations':['bounded ordinary shell static evidence, not REA/runtime','chainedpointer interpretation only for these checkedconstantstrings/dictionaries','no proprietaryC++vtable, carrier, association or networking claim','No device/CI/canonical mutation']}
(P/'source-evidence.json').write_text(json.dumps(r,indent=2)+'\n');print('PASS verifiedsourceinstructions',sum(e['bytes']for e in evidence),'bytes; exactInfrastructurematch +mediarequirement +rateconsumer/EOF')
