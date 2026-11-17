# Fuel board guard correction

Normal CI defines KERNEL and ARM64_BOARD_CONFIG_SWEET, not NORD2 directly. Three canonical helper TUs now include board_config.h under KERNEL before testing NORD2. Active copies match; stripping only wrapper reproduces reviewed private hashes.

compile-promoted.py removes forced -DNORD2 and uses CI-derived kernel flags/headers for C and C++. Expected ARM64 exports nbg_collect, nbg_i2c_read, nord2_battery_platform_sample are asserted. Changing only selected board to T8103 produces empty helper objects. SweetBattery also compiles without forced NORD2. Local C++ retains documented typedallocator builtin compatibility macros; Apple CI remains final compiler validation.

Canonical ASan/UBSan focused tests pass. No hardware, CI, generator or behavior changes.
