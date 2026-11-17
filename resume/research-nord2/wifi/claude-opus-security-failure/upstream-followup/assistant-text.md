I'll read the key source files for the AKS wrap/unwrap analysis.

The 0038 gap is confirmed: enum has `kAKSWrapKey=10`/`kAKSUnwrapKey=11` but the `switch` has no cases, so both hit `default → kIOReturnUnsupported` (0xe00002c7), exactly the logged error. Let me check the oosexgang keystore implementation and the relevant IO80211 compat pieces.

Both upstream implementations use the identical wrap scheme. Let me confirm the exact AKS error shapes in the current snapshot and check the structureInput descriptor path, which matters for the shape securityd actually uses.

WrapKey (sel 10) fails 157 times; UnwrapKey (sel 11) is never reached — consistent with nothing ever being stored. Let me check the first occurrence's context and whether securityd interacts earlier at boot (the "late initialization gap").

The first WrapKey failure (line 1188) is at early daemon bring-up — well before any Wi-Fi join — so the keychain is broken from boot. Let me confirm no *other* AKS selector is failing for securityd/wifid, which would be an additional gap.

securityd WrapKey is the only Wi-Fi-relevant AKS failure. (profiled sel 31, SpringBoard sel 8/32 are unrelated UI/profile gaps.) Since 0900 is a 2377-line patch titled "en0 Apple80211 for wifid," let me check what else it touches, in case there's another first-join gap beyond the keystore.

I have enough to finish the bounded analysis. No new reads needed.

# Pre-SET20 blocker: missing AppleKeyStore key-wrap handlers (0038)

## 1. Root cause (high confidence, with one stated caveat)

The executed kernel (0038) **declares** `kAKSWrapKey = 10` and `kAKSUnwrapKey = 11` in the selector enum (`xnu/patches/0038-sweet-applekeystore.patch:62-63`) but the `externalMethod` switch (`:207-243`) has **no `case` for either**. Both fall through to `default: ret = kIOReturnUnsupported;` (`:240-241`) = `0xe00002c7`.

The trace confirms the matching runtime error, repeated 157 times, the first at early daemon bring-up (`attempt-snapshot.log:1188`), long before any Wi-Fi join:

```
SWEET AKS: securityd sel 10 arg0 0 in 2/32 out 1/40 -> 0xe00002c7
```

`sel 11` (UnwrapKey) **never appears** — consistent: if wrap never succeeds, nothing is ever stored, so nothing is ever unwrapped. The keychain cannot persist a single item from boot onward. When the user types the Wi-Fi password, securityd's `SecItemAdd` must wrap the new item's per-item key via `kAKSWrapKey` → fails → the credential is never stored.

**Causal caveat (do not overstate):** this is a confirmed missing implementation with a confirmed, exactly-matching, repeated error. Whether it is the *specific* reason **SET20 never issues** depends on wifid ordering — i.e. whether wifid writes/reads the PSK through the keychain *before* issuing the association. That final link is not provable from kernel logs. It fits the symptom precisely (password sheet accepted, then "unable to join," zero JOINREQ/SET20), but the deciding evidence is the on-phone wifid report (§5).

## 2. ABI match is exact

Observed `in 2/32 out 1/40`: `scalarInputCount=2`, `structureInputSize=32`, `scalarOutputCount=1`, `structureOutputSize=40`. The upstream handler (identical in Topaz 0900 `SweetKeyStore.cpp` and oosexgang `MiniAppleKeyStore.cpp`) consumes exactly that:
- requires `structureInput != NULL`, `1 ≤ len ≤ 64`, `scalarInputCount ≥ 2` — satisfied (32-byte key);
- emits `8 + len = 40` struct bytes (`"HKwr"` + class byte + 3 zero + mixed key) — matches the 40-byte output buffer securityd pre-sized;
- sets `scalarOutput[0] = scalarInput[1]`, `scalarOutputCount = 1` — matches `out 1`.

`arg0 = 0` is `scalarInput[0]` = the system keybag handle (your `SWEET_AKS_SYSTEM_HANDLE = 0`). `scalarInput[1]` (the protection class) is not logged; the handler echoes it to `scalarOutput[0]`. The 32-byte input is well under the 4096 inband threshold, so `structureInput` is delivered inband and non-NULL — the handler's inband path is correct here; it does **not** handle a descriptor *input* (only `answer()` handles descriptor *output*), which is fine for 32-byte keys but should be noted.

Both upstreams are **byte-identical** in the wrap transform (`"HKwr"` mark, `wrapped[4]=class`, `bytes[i] ^= 0x5a + 37*i`), so a blob wrapped by one unwraps under the other.

## 3. Honest limitations — this is NOT secure keychain

Be explicit in any commit message and code comment:

- The transform is a **fixed, keyless, deterministic XOR mask** (`0x5a + 37*i mod 256`) plus a plaintext header. It is **not** AES Key Wrap (RFC 3394) despite the 8-byte growth mimicking it, and there is **no SEP and no secret**. It is reversible by anyone who can read the blob. The 8-byte prefix carries only a literal `"HKwr"` and the class byte in the clear.
- The mask depends only on byte **offset**, not on the key, the class, the bag, or any device secret. Every wrap of the same plaintext is identical. Do not describe this as encryption or as protecting the password.
- **No caller gating:** neither upstream nor your 0038 checks entitlement or code identity on WrapKey/UnwrapKey. Under the SWEET permissive sandbox any client that can open the AppleKeyStore user client can unwrap any wrapped blob. This is acceptable only as an explicitly-insecure bring-up stand-in.
- **Compatibility contract:** the keychain DB lives in `/private/var`. Whatever scheme ships first becomes a format contract — once items (saved networks) are stored under this XOR scheme, changing the scheme later (e.g. to a real wrap) invalidates every stored item and silently breaks previously-saved Wi-Fi networks. Pick the scheme deliberately now.

Do not invent a different derivation; if you ship this, ship exactly the upstream transform so blobs stay interoperable and the limitation is a known quantity. A genuine implementation would require real class keys / SEP semantics, which this hardware bring-up does not have.

## 4. Minimal cohesive fix

Add two `case` blocks to the existing switch in `0038-sweet-applekeystore.patch` (after `kAKSGetDeviceState`, before `default`). This is the *only* applicable part of Topaz 0900 — the rest of 0900 (`redwood_apple80211.c`, `redwood_net.c`, `redwood_sta.c`, `RedwoodWLAN.cpp`, `MiniIONetworkStack.cpp`) is Topaz's wholesale alternative driver stack and must **not** be adopted; you have your own `nord2-xnu` driver.

```c
case kAKSWrapKey: {
    uint8_t wrapped[8 + 64];
    uint32_t len = in_struct;                 /* inband 32-byte key */
    if (args->structureInput == NULL || len == 0 || len > 64 || in_scalars < 2) {
        ret = kIOReturnBadArgument;
        break;
    }
    memcpy(wrapped, "HKwr", 4);
    wrapped[4] = (uint8_t)args->scalarInput[1];   /* protection class */
    wrapped[5] = wrapped[6] = wrapped[7] = 0;
    memcpy(wrapped + 8, args->structureInput, len);
    for (uint32_t i = 0; i < len; i++)
        wrapped[8 + i] ^= (uint8_t)(0x5a + 37 * i);   /* insecure, keyless, reversible */
    if (out_scalars >= 1) {
        args->scalarOutput[0] = args->scalarInput[1];
        args->scalarOutputCount = 1;
    }
    ret = sweet_aks_struct_out(args, wrapped, 8 + len);
    break;
}
case kAKSUnwrapKey: {
    uint8_t key[64];
    const uint8_t *w = (const uint8_t *)args->structureInput;
    uint32_t len = in_struct;
    if (w == NULL || len <= 8 || len > 8 + sizeof(key) || memcmp(w, "HKwr", 4) != 0) {
        ret = kIOReturnBadArgument;
        break;
    }
    memcpy(key, w + 8, len - 8);
    for (uint32_t i = 0; i < len - 8; i++)
        key[i] ^= (uint8_t)(0x5a + 37 * i);
    args->scalarOutputCount = 0;
    ret = sweet_aks_struct_out(args, key, len - 8);
    break;
}
```

Note: use `in_struct` (your code already computes it as descriptor-or-inband at `:201-202`); for the inband 32-byte case `structureInputSize == in_struct`. Your existing `sweet_aks_struct_out` already handles both inband and descriptor output, so the 40-byte inband output path is covered. The logging line at `:247` will now show `-> 0x0` for sel 10/11.

## 5. The one piece of evidence that closes causation (no new build needed)

When the RAM-rescue HFS collection is authorized, read the **wifid** `AnalyticsData`/crash report (`wifid-1969-12-31-16xxxxx`; RTC epoch is unreliable, order by log position not timestamp) and look at exactly:
- **Exception / termination:** is it a crash (`EXC_*`, signal, Objective-C/`abort()` reason) or a clean error return? A respawn loop would explain the ~15 s re-init cadence seen earlier.
- **Termination reason / abort string** and the top 3–5 **frames**: confirm they sit in Security/keychain (`SecItemAdd`, `securityd`/`trustd` XPC, `MobileKeyBag`, `aks_wrap_key`) rather than in IO80211/association code.
- Any **errSec*/kIOReturn** code threaded up from the wrap failure (e.g. an `errSecNotAvailable`/`errSecDecode`/XPC-interrupted style result).

If those frames are keychain/securityd → this fix is the blocker and should unblock SET20. If wifid instead faults inside association/IO80211, the keychain is a real-but-separate bug and SET20 has a second cause. State which it is from those exact fields before building.

## 6. Other AKS gaps seen (not join-blocking; optional completeness)

Same `default`-unsupported pattern, but **not** on the Wi-Fi path, so do not bundle them as the join fix:
- `profiled sel 31` (0x1f, GetConfiguration) → `0xe...` — config profiles only.
- `SpringBoard sel 8` (0x08, LockDevice) and `sel 32` (0x20, LockDeviceNow) → `0xe...` — UI lock; oosexgang returns success for both. Harmless to add later for a cleaner console, but unrelated to first join.

## 7. Items explicitly ruled out / out of scope
- **Country code (GET51):** withdrawn. Per `review-producer/review.json`, `0x1000546f0` is the real GET51 and its callers are BSP init / NAN / boolean query; unsupported returns immediately and does not block join. My earlier country hypothesis is retracted.
- **5 s PIPE stall:** per `review-service/report.json` the span is PIPE1→PIPE5, not a GET97 duration; not a driver blocker.
- **Topaz 0900 driver files and oosexgang `redwood_*`/`RedwoodWLAN`:** a different, wholesale driver architecture; not applicable without replacing your custom driver — out of scope.
- The two neighboring Topaz patches (0920/0921) are applog/os_log plumbing, not join logic.

**Hardware status unchanged:** no association, key exchange, DHCP, or IP has been attempted or observed. This fix targets the user-space keychain precondition, not the radio path.
