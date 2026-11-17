from pathlib import Path
import difflib,hashlib
P=Path(__file__).resolve().parent
base=(P/'SweetKeyStore.base.cpp').read_text()
assert hashlib.sha256(base.encode()).hexdigest()=='0224ef7ac3fd78511b4cb4369f9099352f80a9b434ae3fd15b5a3dbd1f145d55'
fragment=(P/'key-compat.fragment').read_text()
mark='IOReturn\nAppleKeyStoreUserClient::externalMethod('
assert base.count(mark)==1
out=base.replace(mark,fragment+'\n'+mark)
mark='\n\tIOReturn ret = kIOReturnSuccess;\n\tuint32_t in_scalars'
insert='''
#if defined(NORD2)
	if (selector == kAKSWrapKey || selector == kAKSUnwrapKey) {
		uint8_t scratch[72];
		IOReturn result = sweet_aks_compat_key(selector, args, scratch);
		/* Shapes/status only: never keys, wrapped bytes, classes or credentials. */
		IOLog("SWEET AKS INSECURE: selector=%u input=%llu output=%llu status=%#x\\n", selector,
		    args ? (unsigned long long)args->structureInputSize : 0,
		    args ? (unsigned long long)(args->structureOutputDescriptor ?
		    args->structureOutputDescriptorSize : args->structureOutputSize) : 0, result);
		return result;
	}
#endif
	IOReturn ret = kIOReturnSuccess;
	uint32_t in_scalars'''
assert out.count(mark)==1
out=out.replace(mark,insert)
(P/'proposed/iokit/Kernel/arm/SweetKeyStore.cpp').write_text(out)
patch=''.join(difflib.unified_diff(base.splitlines(True),out.splitlines(True),fromfile='a/iokit/Kernel/arm/SweetKeyStore.cpp',tofile='b/iokit/Kernel/arm/SweetKeyStore.cpp'))
(P/'candidate.patch').write_text(patch)
