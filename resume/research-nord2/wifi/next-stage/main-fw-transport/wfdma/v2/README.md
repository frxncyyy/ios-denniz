# Version 2: polled coherent-error handling

Supersedes the frozen top-level callback candidate manifest b1f575b5e0cd37b60d8ecfcf34556b9a13127f51ec6a09149a4f3069c17b99d9. Its files remain unchanged. The base README provides source ordering and native limitations.

Independent review found that disabled IRQ output leaves no handler to notice coherent DMA errors. This version captures both HOST_INT_STA banks (PA18024200/18025200), with per-capture valid bits, and treats only source-defined RX/TX coherent bits20/21 as fatal. It never acknowledges status, resets or attempts recovery. The initializer refuses inherited coherent faults before mutation, and checks again before publishing initialized state. Transport checks before submission, during every TX/RX poll and immediately before RX rearm. A fault with otherwise valid DIDX/DDONE/event cannot return success.

Source: selected soc3_0.c:720–731 handles TX_COHERENT as abnormal/SER; CODA wf_wfdma_host_dma0.h and wf_wfdma_host_dma1.h HOST_INT_STA fields define RX/TX coherent bits20/21. Checking RX coherent too is a deliberate stricter fail-stop policy. Relative include paths changed only to keep this version separate.

Tests add inherited and late initializer faults and command faults at entry, after submit and after copying the matching event. Callback fault suites and ARM64 compilation were rerun because callback counts changed. No hardware or active source changes occurred.

The PHY peer also found that total command count cannot double as finish-command order: patch finish, FORCE_CAL, then N9 finish is three commands. `finish_phase` now separately orders CID40 then CID41 while `completed` counts all events. A host test covers the actual 40 → 8/EID5 → 41 order. The higher-level PHY adapter must enforce its own single FORCE_CAL and semantic response/EMI backup contract.
