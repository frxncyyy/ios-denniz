# Retained Wi-Fi lifecycle

Associated passive discovery stays on the existing station owner. The worker submits CID03 through the same checked command ring, collects copied beacon/probe responses and a matching genuine SCAN_DONE, and keeps the connected BSS, station, keys, replay state and controlled port. All five RX queues still run. EAPOL, GTK renewal and ordinary Ethernet retain their existing validation and serialized publication. Firmware BSS-absence notifications continue to pause new ordinary TX while off-channel. No boot commands, BSS deactivation or new DMA allocation is replayed.

Requested channel subsets and explicit timing are preserved; no unsolicited channels or active probes are added. UI retention is a separate bounded per-client history policy. Native scan completion is not a claim that every channel has a visible AP or that five-GHz results remain indefinitely valid.

A canceled or timed-out associated scan sends the selected source CID04 cancellation `{sequence,0,0,0}`, SET/no response. Its checked local command retirement permits a canceled/refused callback; it never fabricates firmware SCAN_DONE or firmware acknowledgment. The borrowed result slot stays attached until both scan-request/cancel command DMA and the local lifecycle are retired, including SCAN_DONE racing a pending cancel. Invalid/uncertain DMA still follows the existing strict retained-owner failure path.

SET19 OFF implements logical radio admission, not physical CONNSYS rail gating. It closes ordinary TX/RX admission and BSD carrier synchronously, rejects new scans/joins, cancels queued result delivery and has the retained owner finish actual scan cancellation, key deletes, peer/BSS teardown and ring retirement. Firmware/radio resources remain owned. ON reopens scan admission, and a returned station session schedules a bounded fresh scan before another fresh user-supplied join. The actual closed receipt timestamp is immutable; a separate refresh-request timestamp budgets ON's scan after a long OFF interval. Passwords are not stored for automatic reconnect.

Power requests serialize with link/revoke notices under notice → NSS lock order; the NSS lock is dropped across BSD callbacks. GET19 reports the global logical state. Event1 POWER_CHANGED uses the existing no-primary event-ring shape and tells the setter client to refresh state. A global power epoch invalidates scan completion/history on every client across OFF/ON, including a client that did not issue the toggle. Event delivery does not imply physical power-off, teardown completion, authorization, DHCP or IP.

Run the focused offline tests:

```
python3 tools/test-nord2-wifi-lifecycle.py --log-dir /tmp/nord2-lifecycle
python3 tools/test-nord2-wifi-lifecycle-client.py --log /tmp/nord2-lifecycle-client.log
```

The owner test uses the actual production worker/binders and real crypto/codec with synthetic AP/DMA callbacks: held scan DMA with authenticated GTK renewal, protected Group2, ordinary data and replay-validated RX, stale SCAN_DONE, timeout/cancel, SCAN_DONE before cancel-DDONE, queued-join OFF, OFF during normal scan, rapid concurrent OFF/ON, long-OFF fresh-scan and two fresh joins. The IOUC test uses actual envelope/dispatch/ring/cache code with a typed native power double. Neither test claims live radio, association, DHCP or physical power results.
