# Pending bounded combined-kernel review

No artifact/source commit is bound. No verdict or image gate exists here.
The extractor reuses only the existing Mach-O reader, not prior PASS state.

Inspect nine selected bodies and their actual direct calls. Missing/inlined symbols
remain unresolved and need an explicit bounded replacement; never infer a match.

| Body | Critical linked semantics |
|---|---|
| nst_prepare | exact terminal scan receipt/cursors, actual CAP indices, one attempt, copied profile |
| nst_step | one TX publication, DMA ownership/erasure, separate air result, source RX group/error/CCMP/PN bounds, peer activation, first-beacon PM latch |
| nst_authorize | actual STA, PTK event, one-time PTK/GTK replay installed, no inflight command, no loss/cleanup |
| nst_cleanup_begin | once-only <=30s, closed port, published DMA deadline still binding |
| nwn_event | real typed receipts, EID24 vs DMA, GTK RSC, source grant-minus10ms and actual abort restoration |
| nwo_complete | exact ticket/generation/receipt kind, stale receipts retire only, no inline advancement |
| ncd_worker (inlined nss_owner_loop) | same worker/lock, scan exclusion, selected copied target, bounded16 RX, port revoke, actual carrier result before JOINED, callbacks outside queue lock |
| ncd_normal_owner_window | exact retained native object/thread/init and transport lease, no new broad IO owner |
| nord2_wifi_join_disconnect | synchronous port/queue/carrier closure, lock ordering and generation |

Read helper targets reached by these bodies only when needed to resolve a branch;
record each such bounded extension. Do not disassemble unrelated registry, crypto,
scan or full kernel paths. Root independently owns generated-source equality,
compile flags and ISA checks. Native compiler variation is not itself a defect.

Artifact invocation requires root's successful CI metadata, exact source commit,
size and SHA. The extraction result remains pending manual linked review. It does
not claim packet/RF success, reconnection, firmware retirement or DHCP evidence.

Focused owner.arm64.o currently inlines nss_owner_loop into ncd_worker; the latter
is the bounded selected worker body. Actual linked symbol placement still needs
verification against the future exact artifact.
