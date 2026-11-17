from pathlib import Path
import sys,importlib.util,struct,hashlib,tempfile,subprocess,ctypes,json,copy
R=Path.cwd();P=Path(__file__).parent;sys.path.insert(0,str(R/'tools'));import adt
spec=importlib.util.spec_from_file_location('identity',R/'tools/nord2-wifi-identity.py');identity=importlib.util.module_from_spec(spec);spec.loader.exec_module(identity)
tool_sha=hashlib.sha256((R/'tools/nord2-wifi-identity.py').read_bytes()).hexdigest();payload=(R/'research/nord2/wifi-ios-security/ios/payload.bin').read_bytes();assert hashlib.sha256(payload).hexdigest()=='07f369366eea4ad8ec15fb22b457a9408262fa806774bf6427707113bd76b258';off,n=struct.unpack_from('<QQ',payload,0x20);blob=payload[off:off+n];assert hashlib.sha256(blob).hexdigest()=='9b7ef4215dbe04cb64f2474f79181212ae8c8ef93e0e210f6bbef14c70340451'
root,end=adt.parse(blob);assert end==len(blob);chosen=identity.single_child(root,'chosen');assert chosen.prop(identity.IDENTITY) is None;board=chosen.prop(identity.CALIBRATION);assert hashlib.sha256(board).hexdigest()==identity.BOARD_SHA256
result=identity.transform(blob);assert result==identity.transform(result);new,_=adt.parse(result);newchosen=identity.single_child(new,'chosen');observed=newchosen.prop(identity.IDENTITY);assert observed is not None and len(observed)==6
# This is the actual compiled native selection helper, not a Python mirror.
with tempfile.TemporaryDirectory(prefix='nord2-mg-check-') as t:
 lib=Path(t)/'native.so';subprocess.run(['clang','-shared','-fPIC','-Wall','-Wextra','-Werror','-I',str(R/'shim/nord2-xnu'),str(R/'shim/nord2-xnu/wifi_normal_init.c'),'-o',str(lib)],check=True)
 class Address(ctypes.Structure):_fields_=[('mac',ctypes.c_ubyte*6),('source',ctypes.c_int)]
 dll=ctypes.CDLL(str(lib));fn=dll.nni_address_select;fn.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_void_p,ctypes.POINTER(Address)];native=Address();buf=ctypes.create_string_buffer(board);assert fn(buf,len(board),None,ctypes.byref(native))==0 and native.source==1 and bytes(native.mac)==observed
# Every other original ADT byte/property is preserved after removing the sole addition.
newchosen.props=[p for p in newchosen.props if p[0]!=identity.IDENTITY];assert adt.serialize(new)==blob
checks=3
for mode in range(12):
 x=copy.deepcopy(root);ch=identity.single_child(x,'chosen');arm=identity.single_child(x,'arm-io');wlan=identity.single_child(arm,'wlan')
 if mode==0:x.children.remove(ch)
 elif mode==1:x.children.append(copy.deepcopy(ch))
 elif mode==2:ch.props.append((identity.CALIBRATION,0,board))
 elif mode==3:ch.props=[(k,1 if k==identity.CALIBRATION else f,v) for k,f,v in ch.props]
 elif mode==4:ch.props=[(k,f,v[:-1] if k==identity.CALIBRATION else v) for k,f,v in ch.props]
 elif mode==5:ch.props=[(k,f,bytes([v[0]^1])+v[1:] if k==identity.CALIBRATION else v) for k,f,v in ch.props]
 elif mode==6:ch.props.append((identity.IDENTITY,1,observed))
 elif mode==7:ch.props.append((identity.IDENTITY,0,observed[:-1]))
 elif mode==8:ch.props.append((identity.IDENTITY,0,bytes([observed[0]^2])+observed[1:]))
 elif mode==9:arm.children.remove(wlan)
 elif mode==10:arm.children.append(copy.deepcopy(wlan))
 else:wlan.props=[(k,f,b'wrong\0' if k=='compatible' else v) for k,f,v in wlan.props]
 try:identity.transform(adt.serialize(x));raise AssertionError('accepted malformed case '+str(mode))
 except ValueError:checks+=1
assert hashlib.sha256((R/'tools/nord2-wifi-identity.py').read_bytes()).hexdigest()==tool_sha
(P/'identity-review.json').write_text(json.dumps({'verdict':'PASS','tool_sha256':tool_sha,'payload_sha256':hashlib.sha256(payload).hexdigest(),'old_adt_sha256':hashlib.sha256(blob).hexdigest(),'new_adt_sha256':hashlib.sha256(result).hexdigest(),'native_helper_sha256':hashlib.sha256((R/'shim/nord2-xnu/wifi_normal_init.c').read_bytes()).hexdigest(),'checks':checks,'factory_address_printed':False,'provider_equivalent':'chosen property binary6, exact compiled native BOARD address equality, sole addition preserves old ADT, idempotent','negative_cases':12,'timing':'offline ADT property precedes every process; controller verifies same identity later','limits':'No live MG execution; no user cache reset; registry concurrent publisher is not part of offline transform'},indent=2)+'\n');print('PASS actual executed ADT + compiled native BOARD selection; 12 malformed/conflict refusals; no private value output')
