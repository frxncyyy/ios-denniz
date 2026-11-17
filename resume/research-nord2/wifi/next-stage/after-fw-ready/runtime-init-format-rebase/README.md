# Private capability runtime rebase after INIT format changes

This separate default-off package rebases the reviewed runtime-txdone-rebase onto the current canonical RX diagnostic ABI and reviewed INIT type/CID contract. Original packages remain frozen. No activation, boot argument, readiness state, device action or CI is introduced.

Dispatcher and capability worker source remain byte-identical. The normal capability protocol retains its existing E000 requirement: measured INIT type7 is not projected onto normal capability events. Actual retained main FW_READY, pool/ROM/current ownership receipts, one-use dedicated worker, bounded CID8a/EIDec query, shared halt and parked terminal success remain required. FW_READY has not been established by this private preparation.

Canonical snapshots include RX diagnostics and all four promoted INIT-format files where represented in this runtime dependency set. INIT changes accept exact7/E000 and source EID/sequence/status, without the unsupported CID echo assertion. They do not fabricate an actual successful INIT response. PHY exact60 remains the separate reviewed runtime limit.

All35 selected size/offset fields are compared across separate canonical and snapshot translation units. Updated sizes are nwt1176, nwi3040, nwfn7440; transport offset1864 remains. RX fields extend the transport and shift downstream native ownership by72 bytes. Source pins bind exact current bytes independently of parent commit timing.

Run test.py for host-only synthetic sanitizer tests; generated test logs are not hardware evidence. No production integration or normal-context ARM64 proof is claimed by this rebase. Parent must rebuild all participating TUs and review actual linked offsets before any future activation. No scan, interface, resource, regulatory or networking readiness is supplied.
