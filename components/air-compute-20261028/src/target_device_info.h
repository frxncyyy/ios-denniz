#ifndef TEGU_TARGET_DEVICE_INFO_H
#define TEGU_TARGET_DEVICE_INFO_H
/* Donor iOS16 protocol encoding: ^{MTLTargetDeviceArch=QI*}.
 * MTLPipelineLibraryBuilder::newLibraryWithFile fallback (181a3f628) zeros
 * bytes0..11 and supplies a device-name C string at16. Its normal branch trusts
 * targetDeviceInfo nonnull. _MTLDevice's default getter returns NULL.
 * _MTLPipelineCache initWithFilePath:readOnly:deviceInfo: reads word8 as cache
 * version. Match the donor fallback's zero values: no Apple target is claimed.
 * These are cache identity fields, not Mali shader compilation settings.
 */
struct MTLTargetDeviceArch {
 unsigned long long identifier;
 unsigned version;
 const char *name;
};
_Static_assert(sizeof(struct MTLTargetDeviceArch)==24,"donor target device size");
_Static_assert(__builtin_offsetof(struct MTLTargetDeviceArch,version)==8,"donor version offset");
_Static_assert(__builtin_offsetof(struct MTLTargetDeviceArch,name)==16,"donor name offset");
#define TEGU_TARGET_CACHE_NAME "Nord2MaliG77"
#endif
