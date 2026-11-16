/*
 * Built-in AppleKeyStore for SWEET: a device without a passcode whose
 * system keybag is always loaded and unlocked.
 *
 * Apple's AppleKeyStore kext (closed, SEP-backed) owns the keybags behind
 * data protection. libaks (AppleKeyStore.framework, statically in keybagd)
 * opens IOServiceMatching("AppleKeyStore") with type 0, calls selector 0,
 * then one selector per aks_* function (iOS 16.7 numbering below). keybagd
 * --init asks for the system bag (14) and only loads systembag.kb when the
 * answer is kIOReturnNotFound; answering with a handle is the path a
 * userspace reboot takes, so no bag file is needed.
 *
 * Every call is logged (selector, caller, argument shapes, result) so the
 * next selectors the system needs show up on the console; unknown ones fail
 * with kIOReturnUnsupported.
 */
#include <pexpert/arm64/board_config.h>

#if defined(SWEET)
#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <libkern/c++/OSString.h>
#include <sys/proc.h>

/* libaks selectors (AppleKeyStore.framework, iOS 16.7). */
enum {
	kAKSOpen             = 0,
	kAKSCreateBag        = 2,
	kAKSSaveBag          = 3,
	kAKSUnloadBag        = 4,
	kAKSSetSystem        = 5,
	kAKSLoadBag          = 6,
	kAKSGetLockState     = 7,
	kAKSUnlockDevice     = 9,
	kAKSWrapKey          = 10,
	kAKSUnwrapKey        = 11,
	kAKSUnlockBag        = 12,
	kAKSLockBag          = 13,
	kAKSGetSystem        = 14,
	kAKSGetDeviceState   = 17,
	kAKSGetExtDeviceState = 35,     /* aks_get_extended_device_state */
	kAKSAssertHold       = 26,
	kAKSAssertDrop       = 27,
	kAKSUnloadSessionBags = 55,
	kAKSNotifyStateSet   = 102,
	kAKSNotifyStateIsSet = 103,
	kAKSAssertConsume    = 101,     /* aks_assert_consume */
};

/* The system (device) bag handle. */
#define SWEET_AKS_SYSTEM_HANDLE 0

/*
 * aks lock state bits (aks_get_lock_state): bit 2 is "unlocked since boot"
 * (MobileKeyBag MKBDeviceUnlockedSinceBoot); clear lock bits = unlocked.
 */
#define SWEET_AKS_LOCK_STATE    0x4

class AppleKeyStore : public IOService {
	OSDeclareDefaultStructors(AppleKeyStore);

public:
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
};

OSDefineMetaClassAndStructors(AppleKeyStore, IOService);

bool
AppleKeyStore::start(IOService *provider)
{
	if (!IOService::start(provider)) {
		return false;
	}
	setProperty(kIOUserClientClassKey, "AppleKeyStoreUserClient");
	registerService();
	IOLog("SWEET AKS: AppleKeyStore registered (no passcode, unlocked)\n");
	return true;
}

class AppleKeyStoreUserClient : public IOUserClient {
	OSDeclareDefaultStructors(AppleKeyStoreUserClient);

public:
	IOReturn clientClose(void) APPLE_KEXT_OVERRIDE;
	IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *args,
	    IOExternalMethodDispatch *dispatch, OSObject *target,
	    void *reference) APPLE_KEXT_OVERRIDE;
};

OSDefineMetaClassAndStructors(AppleKeyStoreUserClient, IOUserClient);

IOReturn
AppleKeyStoreUserClient::clientClose(void)
{
	terminate();
	return kIOReturnSuccess;
}

/* Copy len bytes to the caller's output structure (inband or descriptor). */
static IOReturn
sweet_aks_struct_out(IOExternalMethodArguments *args, const void *buf, uint32_t len)
{
	if (args->structureOutputDescriptor != NULL) {
		if (len > args->structureOutputDescriptor->getLength()) {
			return kIOReturnNoSpace;
		}
		args->structureOutputDescriptor->writeBytes(0, buf, len);
		args->structureOutputDescriptorSize = len;
		return kIOReturnSuccess;
	}
	if (args->structureOutput == NULL || len > args->structureOutputSize) {
		return kIOReturnNoSpace;
	}
	memcpy(args->structureOutput, buf, len);
	args->structureOutputSize = len;
	return kIOReturnSuccess;
}

/* Append SEQUENCE { UTF8String key, INTEGER value } (short forms only). */
static uint32_t
sweet_aks_der_pair(uint8_t *p, const char *key, uint8_t value)
{
	uint32_t klen = (uint32_t)strlen(key);
	uint32_t n = 0;
	p[n++] = 0x30;
	p[n++] = (uint8_t)(2 + klen + 3);
	p[n++] = 0x0c;
	p[n++] = (uint8_t)klen;
	memcpy(p + n, key, klen);
	n += klen;
	p[n++] = 0x02;
	p[n++] = 0x01;
	p[n++] = value;
	return n;
}

/*
 * Device state (selector 17): a DER SET of key/INTEGER pairs that libaks
 * decode_extended_state reads: ss state, sls lock state (MKBGetDeviceLockState:
 * 0 = unlocked), sb backoff, sfa failed attempts, sgs generation, sas
 * assertions, sgpe grace period, srcd recovery countdown.
 */
static IOReturn
sweet_aks_device_state(IOExternalMethodArguments *args)
{
	static const struct {
		const char *key;
		uint8_t value;
	} fields[] = {
		{ "ss", SWEET_AKS_LOCK_STATE }, { "sls", 0 }, { "sb", 0 }, { "sfa", 0 },
		{ "sgs", 0 }, { "sas", 0 }, { "sgpe", 0 }, { "srcd", 0 },
	};
	uint8_t buf[128];
	uint32_t n = 2;
	for (unsigned int i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
		n += sweet_aks_der_pair(buf + n, fields[i].key, fields[i].value);
	}
	buf[0] = 0x31;
	buf[1] = (uint8_t)(n - 2);
	return sweet_aks_struct_out(args, buf, n);
}

static uint32_t
sweet_aks_struct_out_size(IOExternalMethodArguments *args)
{
	return args->structureOutputDescriptor != NULL ?
	       (uint32_t)args->structureOutputDescriptor->getLength() : args->structureOutputSize;
}

#if defined(NORD2)
/*
 * INSECURE NORD2 bring-up compatibility, derived from HackiOS MiniAppleKeyStore
 * (oosexgang/hackios-xnu-kernel 8e611870, APSL 2.0), also Topaz 0900 at e4772911.
 * This is their exact HKwr/class/zero-reserved/fixed-XOR storage format.
 * It provides NO encryption, authentication, SEP or secret class keys.
 * Persisted blobs remain readable across boots with this same public format;
 * foreign formats fail, and no keychain/bag reset or migration is performed.
 */
static void
sweet_aks_compat_wipe(void *buffer, size_t length)
{
	volatile uint8_t *p = (volatile uint8_t *)buffer;
	while (length-- != 0) {
		*p++ = 0;
	}
}

/* Preflight performs no caller-buffer or descriptor mutation. Input keys are
 * bounded inband only; supporting an input descriptor is deliberately refused.
 * Only the existing fixed system bag is implemented. Protection class is opaque:
 * its full scalar is echoed, while the upstream header records only its low byte.
 * Comparing that byte on unwrap is format consistency, NOT authentication.
 */
static IOReturn
sweet_aks_compat_key(uint32_t selector, IOExternalMethodArguments *args,
    uint8_t scratch[72])
{
	sweet_aks_compat_wipe(scratch, 72);
	if (args == NULL || args->version != kIOExternalMethodArgumentsCurrentVersion ||
	    args->selector != selector || (selector != kAKSWrapKey && selector != kAKSUnwrapKey) ||
	    args->scalarInputCount != 2 || args->scalarInput == NULL ||
	    args->scalarInput[0] != SWEET_AKS_SYSTEM_HANDLE ||
	    args->scalarOutputCount > (selector == kAKSWrapKey ? 1u : 0u) ||
	    (args->scalarOutputCount != 0 && args->scalarOutput == NULL) ||
	    args->asyncReferenceCount != 0 || args->asyncWakePort != 0 ||
	    args->structureVariableOutputData != NULL ||
	    args->structureInputDescriptor != NULL || args->structureInput == NULL) {
		return kIOReturnBadArgument;
	}
	const uint8_t *input = (const uint8_t *)args->structureInput;
	size_t length = args->structureInputSize;
	const bool wrap = selector == kAKSWrapKey;
	if (wrap ? (length == 0 || length > 64) : (length <= 8 || length > 72)) {
		return kIOReturnBadArgument;
	}
	if (!wrap && (memcmp(input, "HKwr", 4) != 0 ||
	    input[4] != (uint8_t)args->scalarInput[1] || input[5] || input[6] || input[7])) {
		return kIOReturnBadArgument;
	}
	const size_t used = wrap ? length + 8 : length - 8;
	IOMemoryDescriptor *output = args->structureOutputDescriptor;
	if (output != NULL) {
		/* MIG retains an inactive inband pointer even for descriptor output;
		 * size zero makes it inactive. Never dereference it in this branch. */
		if (args->structureOutputSize != 0 ||
		    args->structureOutputDescriptorSize != output->getLength()) {
			return kIOReturnBadArgument;
		}
		if (args->structureOutputDescriptorSize < used) {
			return kIOReturnNoSpace;
		}
	} else {
		if (args->structureOutputDescriptorSize != 0) {
			return kIOReturnBadArgument;
		}
		if (args->structureOutput == NULL || args->structureOutputSize < used) {
			return kIOReturnNoSpace;
		}
	}
	if (wrap) {
		memcpy(scratch, "HKwr", 4);
		scratch[4] = (uint8_t)args->scalarInput[1];
		/* All three reserved bytes were zeroed before any input copy. */
		memcpy(scratch + 8, input, length);
	} else {
		memcpy(scratch, input + 8, used);
	}
	for (size_t i = 0, n = wrap ? length : used; i < n; i++) {
		scratch[(wrap ? 8 : 0) + i] ^= (uint8_t)(0x5a + 37 * i);
	}
	IOReturn result = kIOReturnSuccess;
	if (output != NULL) {
		result = output->prepare(kIODirectionIn);
		if (result == kIOReturnSuccess) {
			IOByteCount copied = output->writeBytes(0, scratch, used);
			IOReturn completed = output->complete(kIODirectionIn);
			if (completed != kIOReturnSuccess) {
				result = completed;
			} else if (copied != used) {
				result = kIOReturnIOError;
			}
		}
	} else {
		memcpy(args->structureOutput, scratch, used);
	}
	/* Includes prepare, partial write and completion failure paths. */
	sweet_aks_compat_wipe(scratch, 72);
	if (result == kIOReturnSuccess) {
		if (output != NULL) {
			args->structureOutputDescriptorSize = (uint32_t)used;
		} else {
			args->structureOutputSize = (uint32_t)used;
		}
		if (wrap && args->scalarOutputCount == 1) {
			args->scalarOutput[0] = args->scalarInput[1];
		}
	}
	return result;
}
#endif /* NORD2 */


IOReturn
AppleKeyStoreUserClient::externalMethod(uint32_t selector, IOExternalMethodArguments *args,
    __unused IOExternalMethodDispatch *dispatch, __unused OSObject *target,
    __unused void *reference)
{
#if defined(NORD2)
	if (selector == kAKSWrapKey || selector == kAKSUnwrapKey) {
		uint8_t scratch[72];
		IOReturn result = sweet_aks_compat_key(selector, args, scratch);
		/* Shapes/status only: never keys, wrapped bytes, classes or credentials. */
		IOLog("SWEET AKS INSECURE: selector=%u input=%llu output=%llu status=%#x\n", selector,
		    args ? (unsigned long long)args->structureInputSize : 0,
		    args ? (unsigned long long)(args->structureOutputDescriptor ?
		    args->structureOutputDescriptorSize : args->structureOutputSize) : 0, result);
		return result;
	}
#endif
	IOReturn ret = kIOReturnSuccess;
	uint32_t in_scalars = args->scalarInputCount;
	uint32_t in_struct = args->structureInputDescriptor != NULL ?
	    (uint32_t)args->structureInputDescriptor->getLength() : args->structureInputSize;
	uint32_t out_scalars = args->scalarOutputCount;
	uint32_t out_struct = sweet_aks_struct_out_size(args);
	uint64_t arg0 = in_scalars > 0 ? args->scalarInput[0] : 0;

	switch (selector) {
	case kAKSOpen:
	case kAKSUnloadBag:
	case kAKSSetSystem:
	case kAKSUnlockDevice:
	case kAKSUnlockBag:
	case kAKSLockBag:
	case kAKSAssertHold:
	case kAKSAssertDrop:
	case kAKSAssertConsume:
	case kAKSUnloadSessionBags:
	case kAKSNotifyStateSet:
		args->scalarOutputCount = 0;
		break;
	case kAKSGetSystem:
		if (out_scalars < 1) {
			ret = kIOReturnBadArgument;
			break;
		}
		args->scalarOutput[0] = SWEET_AKS_SYSTEM_HANDLE;
		args->scalarOutputCount = 1;
		break;
	case kAKSGetLockState:
		if (out_scalars < 1) {
			ret = kIOReturnBadArgument;
			break;
		}
		args->scalarOutput[0] = SWEET_AKS_LOCK_STATE;
		args->scalarOutputCount = 1;
		break;
	case kAKSNotifyStateIsSet:
		/* Nothing was ever set: answer "not set". */
		if (out_scalars < 1) {
			ret = kIOReturnBadArgument;
			break;
		}
		args->scalarOutput[0] = 0;
		args->scalarOutputCount = 1;
		break;
	case kAKSGetDeviceState:
	case kAKSGetExtDeviceState:
		args->scalarOutputCount = 0;
		ret = sweet_aks_device_state(args);
		break;
	default:
		ret = kIOReturnUnsupported;
		break;
	}

	char name[32];
	proc_selfname(name, sizeof(name));
	IOLog("SWEET AKS: %s sel %u arg0 %llu in %u/%u out %u/%u -> 0x%x\n", name, selector,
	    arg0, in_scalars, in_struct, out_scalars, out_struct, ret);
	return ret;
}
#endif /* SWEET */
