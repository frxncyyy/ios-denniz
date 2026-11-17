# Exact crash finding (original same-IPSW bytes)

All eight report backtraces have the same wifid offsets 0x930e4, 0xf11a0, 0x130a88, 0x134ed4 and UUID f0919f06-43c6-3723-aff2-411233365292. Local selected wifid LC_UUID matches; SHA256 0ec6ad6ee120fb107510393c485a19ceaf54ef8dc7da479899a8cf7b93cdf871.

At return PC 0x1000930e4, the preceding call at 0x1000930e0 is dictionary insertion. Arguments: x0=x25 dictionary; x1=constant CFString 0x100220040 **PRIVATE_MAC_ADDRESS_VALUE**; x2=[x19+480]. The key is constant/non-null. The candidate nil value is therefore cached private-MAC object at object offset480, not country code. Subsequent instructions insert PRIVATE_MAC_ADDRESS_VALID and PRIVATE_MAC_ADDRESS.

Field480 has an initialization candidate at0x100062da8 (return from imported function0x100175b58); a clear at0x10008ed4c. Both are being traced; no assertion yet that an unsupported driver GET directly produced nil. Current evidence does not justify fabricating a randomized MAC or enabling unsupported private-address rotation.

Read-only investigation; no canonical/device/CI changes. Full source-byte and eight-signature receipts follow.

## Exact producer identified

At 0x100062d98–0x100062da8, the original daemon calls `MGCopyAnswer(CFSTR("WifiAddressData"), NULL)` and stores the returned CF object at manager+480. The literal is decoded from original CFString 0x100221e40; import stub 0x100175b58 is `_MGCopyAnswer`. A NULL result takes 0x100062dac directly to 0x100062e1c, leaving that field NULL without rejecting manager creation. A non-NULL result is consumed with `_CFDataGetBytePtr` (stub 0x1001753a8), then exactly six bytes are copied to manager+472/+476 at 0x100062dd4–0x100062de0. No address bytes were extracted or logged.

The other manager+480 clearing sequence at 0x10008ed40–0x10008ed4c conditionally calls `_CFRelease` then stores NULL amid teardown of many other manager-owned objects. This is consistent with object destruction, not a country-query error. The direct initialization and unguarded dictionary insertion provide a concrete missing-MobileGestalt-data hypothesis; the actual runtime return remains inferred from the exception rather than captured.

Consequently an IOUC GET-MAC response alone does not prove this exact producer repaired: the queried ABI here is MobileGestalt `WifiAddressData` returning a retained CFData object containing the six-byte real factory Wi-Fi address. Any eventual provider must use authenticated actual device metadata, preserve proper CF ownership, and avoid representing a constant or fabricated private address. Whether MobileGestalt can obtain that address from the current registry/property path still requires its selected provider evidence.
