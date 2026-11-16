# iOS arm64 bundle ABI/link stub

This host has no iPhoneOS SDK and no Metal.framework/Foundation headers.  The
real source-only shim (`gpu/metal/Nord2Metal.m`) and its tested C core can still
be compiled and linked as an arm64 iOS Mach-O bundle with the Clang builtin
freestanding headers plus minimal libc declarations:

```sh
LLVM_NM=/path/to/llvm-nm gpu/tools/build_ios_bundle_stub.sh
```

Observed 2026-10-24:

```
Nord2Metal: Mach-O 64-bit arm64 bundle, flags:<NOUNDEFS|DYLDLINK|TWOLEVEL>
LC_BUILD_VERSION: platform 2 (iOS), minos 16.0
symbols: _n2m_device_init, _OBJC_CLASS_$_Nord2MetalDevice
```

The bundle directory also contains a minimal `Info.plist` with a deliberately
local stub identifier.  The test is part of `run_host_tests.sh`.

## What this proves and does not prove

Proves:

* the shim and core compile for `arm64-apple-ios16.0`;
* the exact objects link as an arm64 iOS bundle with the expected class/core
  symbols and Mach-O ABI;
* no SDK or Apple binary is needed for this ABI check;
* the build is reproducible from tracked source.

Does **not** prove:

* loadability: `-undefined dynamic_lookup` leaves Objective-C/Metal symbols
  unresolved and no Metal.framework/Foundation runtime is linked;
* a real Metal plugin: the shim declares its own `NSObject`/`_MTLDevice`
  contract and is compute-only;
* rendering, IOSurface, QuartzCore or presentation behavior;
* anything about the phone.

This is deliberately not installed or copied to the phone.  A real bundle still
requires the iPhoneOS SDK, Metal/Foundation headers, the real Metal runtime,
and the missing render/IOSurface frontend.
