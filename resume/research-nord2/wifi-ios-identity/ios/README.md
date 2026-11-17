# Closed next-image stage: preboot factory identity and insecure keychain compatibility

Target: `shim/out/nord2-xnu/nord2-ios-wifi-identity-keychain-stop23.img`.
This is a fresh private template, not a prepared image. Source commit, successful
CI run, kernel identity, native/linked review, image identity and authorization
are pending. All corresponding gates are false; the runner is unused. No old
USED marker, approval, image, payload, build log, hardware evidence, source bundle
or linked-success gate was copied from the executed security stage.

The intended cohesive change is the actual pinned board MAC present before any
userland process, native registry equality with that same board identity, and
the reviewed NORD2 AKS10/11 compatibility operations. The AKS `HKwr`/fixed-XOR
format is **INSECURE bring-up compatibility: no encryption, authentication, SEP
or secure persistent storage**. Existing UFS/keychain contents are not reset or
silently migrated. This stage claims no actual join, keychain, association,
key-installation, DHCP/IP or Internet success.

The existing539-byte command line is unchanged, including UFS/display flags and
`nord2wifibasic=1 nord2wifiscan=1 nord2wifivcn33=1 nord2wifiios=1`. No new argument
or unrelated UFS, speed, RF/register or transport change is introduced here.
Recovery remains the explicitly pinned human-confirmed restored healthy6e4 security
image plus d78b RAM rescue. These are recovery identities, not authorization;
new exact-image/recovery approval remains false.

`build-image.sh` retains the existing calibration injection, then invokes
`tools/nord2-wifi-identity.py` on that calibrated ADT before packing. The verifier
first reproduces the immutable e0d3 historical non-service baseline, then the
actually executed6e4 calibrated+wlan baseline (payload07f36936, ADT19160 bytes
SHA9b7ef421...). It independently requires exactly one additional chosen property:
`mac-address-wifi0`, flags0/raw6, equal to the pinned6146-byte calibration's bytes4:10.
Removing only that property must recover the prior calibrated ADT byte-for-byte;
removing calibration too must recover the original service baseline. Address
bytes are never printed. Loader/kernel/ADT/rescue/header checks are preserved.

Root must supply the exact committed source/CI/kernel identity, complete source
pins (including the identity tool, registry, generator and0083), fresh diagnostic
and linked-review pins, then separately verified prepared image and exact human
authorization. `source-proposals.json` records reviewed private inputs only and
is explicitly not proof of a committed/compiled native implementation.

Offline template checks: `python3 test-stage.py` passed47 checks, one positive
using the actual executed calibrated ADT, and12 negative mutation cases. Shell
syntax and Python AST checks passed; closed `verify-image.py --preflight` refuses
before image access. Parser/core dependencies remain byte-identical copies of
the reviewed prior stage. No builder, runner, device, CI or canonical source was
invoked or changed by stage preparation.
