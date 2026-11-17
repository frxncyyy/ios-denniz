# Final startup and diagnostics review

Canonical native readiness and worker wait reviewed after root promotion. Release/acquire publication prevents snapshots racing initial NI GPIO writes; prepare returns WAIT without mapping or setting attempted/worker state. Successful publication is immutable. Permanent prepare failure is not retried. Worker initializes live nonce before its bounded 60-second dependency wait, services commands during that wait, and invokes nau_hw_init once.

Additional private diagnostic-only corrections (not promoted here):
- audio_device.c.diagnostics.patch: log nau_hw_status derived saved fields, without additional MMIO.
- audio_hw.c.diagnostics.patch: record the early cold gate and merge partial bus/AFE uncertainty in the status accessor.

Focused actual-function host regression test-diagnostics.c + ready-diagnose.inc passes ASan/UBSan: saved phase/op, bus stage/failure, last physical address, partial-init uncertainty, and early cold gate are retained; logging causes zero MMIO. Both modified source TUs compile with normal NORD2 ARM64 kernel flags. Existing renamed full-model fixture requires test-only -Wno-return-type and -Wno-unused-function.

No additional hardware blocker can be established from the failed prepublication attempt: no snapshot or amp transfer occurred. Hardware guards remain intact. Exact publisher placement in active sweet_touch remains root-owned and must follow ni_wifi_power_probe and all initial touch/GPIO writes, before polling.
