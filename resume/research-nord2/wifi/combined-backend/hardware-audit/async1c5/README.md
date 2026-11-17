# Executed async1c5 receive DMA audit

The pinned real receipt shows no proven DMA configuration defect. All four DMA0 rings have correct source hardware mapping, count, CIDX and final prefetch extent, and untouched current DDONE0 descriptors at both snapshots. DMA1 delivered thirteen sleepy notifications and matching scan-done. Neither the host nor firmware recorded a beacon. The already executed zero-BSSID change was insufficient.

`python3 audit.py` checks the immutable receipt SHA and 57 source/receipt assertions, then writes `report.json`. It reads local files only. No code, MMIO, device operation or canonical change is proposed by this audit.

Selected Android root: `work/nord2-current-kernel/drivers/misc/mediatek/connectivity/wlan/core/gen4m`.

| Evidence | Exact selected source |
| --- | --- |
| HE80 selection for SoC3 | `Makefile:131..148` |
| Five software RX queues map to DMA0 R0, DMA1 R0, DMA0 R1/R2/R3 | `chips/soc3_0/soc3_0.c:362..413` |
| Extra data/report ring allocation | `chips/soc3_0/soc3_0.c:415..439` |
| Disable/init/prefetch/enable sequence | `os/linux/hif/common/hal_pdma.c:2110..2138` |
| Source CIDX=count-1, owned base/count publication | `os/linux/hif/common/hal_pdma.c:2271..2292` |
| Source RX SDP/SDL/DDONE initialization | `os/linux/hif/common/hal_pdma.c:1879..1915` |
| Selected ring sizes1024/16 and descriptor16 | `os/linux/hif/common/include/hif_pdma.h:118..152` |
| Final explicit DMA0 RX prefetch4/400004/800004/c00004 | `chips/soc3_0/soc3_0.c:791..834` |
| Enable RX/TX for each DMA, preserve unspecified mode fields | `chips/soc3_0/soc3_0.c:474..559` |
| Exact SoC3 GLO_CFG field meanings | `include/chips/coda/soc3_0/wf_wfdma_host_dma0.h:2040..2147` |

Global values `5010b875/5810b875` have RX/TX enable set, DMA busy clear,16-byte descriptors, little endian,32-bit addresses and the source DMA0/DMA1 omit-RX distinction. Two subtleties prevent an unsupported register fix:

- The generic `field_conn2x` union calls bit21 `omit_rx_info_pfet2`, but the exact SoC3 CODA specification marks21..23 RO reserved. Readback bit21 zero is not evidence that a writable receive prerequisite was lost.
- Both actual configurations retain bit20=1. CODA describes a selector for **TXFIFO loopback traffic** in loopback mode. The selected driver and native RMW preserve this field; no source-backed clear operation or demonstrated cause of RF silence was found. Do not clear a mode bit merely because its name contains loopback.

One correction to earlier audit wording: native DATA0 has3840-byte owned buffers, but selected ordinary Android DATA0 uses `CFG_RX_MAX_MPDU_SIZE=2352` (`hal_pdma.c:1980..1983`, `include/config.h:515..527`); DATA1/event/reports use3840. The native larger owned bound is explicit, not exact descriptor-length source parity. It does not explain DIDX0 throughout or firmware beacon counts0, and changing it is not proposed as an empty-scan fix.

Snapshot times do not prove every internal transaction, and no raw returned SDP is used as authority. IRQ output remains deliberately disabled in the polling runtime; RX/TX transfer enable is captured. Firmware RF interpretation and the independently identified LinuxUP CID0a filter/lifecycle gap are separate source audits. Retain these readbacks for a source-backed lifecycle correction, without speculative DMA writes, fake scan results, interface readiness or network claims.
