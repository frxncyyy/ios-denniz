# Future cohesive DHCP stage — CLOSED

Intended image: `shim/out/nord2-xnu/nord2-ios-wifi-dhcp-stop23.img`.
No new source commit, CI, kernel, payload, image or human approval exists for this stage. Identity/schema/prepared/human/RF/recovery gates are FALSE and artifact/approval identities are null. The approved runner is UNUSED. No previous08e6 authorization is inherited.

This private template retains the datapath stage's normal HW0 TXD/TXP and descriptor+payload-token lifetime, secure GTK renewal, infrastructure/FAST/dwell scan support, and bounded RX/TXS/own-client DHCP diagnostic intents. These intents are neither a new implementation approval nor hardware success. The specific DHCP proposal and any additional functional flags remain null/pending until the actual Claude proposal is frozen and independently reviewed. No rate0/PID127/DHCP-lease inference is introduced.

The six original collection readers are unchanged; a seventh supplemental canonical RXERROR reader is added and all are pinned in `reader-dependencies.json`. Local core-receipt.py and core-lifecycle.py are copied byte-identically, including their strict dependency hashes. The legacy full startup reader can reject new RXCOUNTS/RXREJECT rows after Join; preserve that failure as reader-coverage evidence. Check the immutable pre-Join startup prefix independently and retain the other six post-Join readers. Do not weaken filters or treat this known full-log incompatibility as a hardware failure. Native admission, local submission, air ACK, DHCP match and actual IP configuration remain separate observations.

Recovery identities remain d9a44316 primary,6e4ce860 secondary and d78bf9fe RAM ADB rescue, exactly as pinned. No recovery is authorized yet. Never boot stock Android over iOS userdata. Stock boot is a read-only packing/header reference only. Existing UFS/rootfs geometry and installed-image evidence are unchanged; this template performs no filesystem/device work.

Before any build, parent must supply reviewed proposal/source pins, exact successful CI/kernel identity and actual linked/source/schema reviews. Build uses a fresh image/log only, then independent existing-image verification must precede opening prepared gate and requesting new exact-image boot-only human/RF/conditional-recovery authorization. Parent alone owns these transitions and device work.

`closed-preflight-test.py` checks closed/null gates, exact reader dependencies and fail-before-work behavior of `verify-image.py --preflight`; it invokes no builder, approved runner, CI or device tool. Future builder and runner paths are present only as blocked templates. No image, payload, builder log, hardware log, runner-used marker or inherited receipt is copied.

The collector checks the pinned canonical RXERROR reader hash before writing a snapshot, records its separate exit status as `rx-error`, and preserves malformed-record failures. Missing/budget-exhausted RXERROR rows never imply zero errors. A1/destination is hardware metadata, not authenticated payload or BSD delivery.
