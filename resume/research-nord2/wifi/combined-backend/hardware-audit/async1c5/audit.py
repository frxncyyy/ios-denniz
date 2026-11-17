#!/usr/bin/env python3
"""Read-only, pinned receipt/source oracle. Does not access a device."""
import hashlib
import json
from pathlib import Path
import re

HERE = Path(__file__).resolve().parent
ROOT = next(p for p in HERE.parents if (p / "shim/nord2-xnu").is_dir())
ANDROID = ROOT.parent / "nord2-current-kernel/drivers/misc/mediatek/connectivity/wlan/core/gen4m"
RECEIPT = ROOT / "research/nord2/wifi-async-scan/ios/backend-first-receipt.log"
EXPECTED_SHA = "bbc7ec70c6709c9c1a2c4ea910dae2ac4bc809bc2b42406394fb472b48422560"
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
assert sha(RECEIPT) == EXPECTED_SHA
text = RECEIPT.read_text()
number = lambda s: int(s, 0)
checks = []
def check(name, condition):
    assert condition, name
    checks.append(name)

source_files = [
    "chips/soc3_0/soc3_0.c", "include/nic/mt66xx_reg.h",
    "include/chips/coda/soc3_0/wf_wfdma_host_dma0.h",
    "include/chips/coda/soc3_0/wf_wfdma_host_dma1.h",
    "os/linux/hif/common/hal_pdma.c", "os/linux/hif/common/include/hif_pdma.h",
    "include/config.h", "Makefile",
]
source = {f: (ANDROID / f).read_text() for f in source_files}
check("exact SoC3 bit21..23 are reserved", "RESERVED21[23..21] - (RO) Reserved bits" in source[source_files[2]])
check("loopback selector is bit20", "0x00100000 /* CSR_LBK_RX_Q_SEL_EN[20] */" in source[source_files[2]])
check("loopback selects TXFIFO traffic", "loopback data from TXFIFO" in source[source_files[2]])
check("Android DATA0 uses MPDU bound", "CFG_RX_MAX_MPDU_SIZE, fgAllocMem" in source[source_files[4]])
check("ordinary MPDU bound is2352", "#define CFG_RX_MAX_PKT_SIZE\t(28 + 2312 + 12" in source["include/config.h"])
check("ordinary MPDU aliases packet bound", "#define CFG_RX_MAX_MPDU_SIZE\tCFG_RX_MAX_PKT_SIZE" in source["include/config.h"])
check("sniffer13k mode disabled", "#define CFG_SUPPORT_SNIFFER_RADIOTAP_13K\t0" in source["include/config.h"])

summary = re.findall(r"^NORD2 WIFI RXDMA: n=(\d+) attempted=(\d+) complete=(\d+) error=(\d+) reason=(\d+) valid=(\S+) cfg=(\S+)/(\S+) started=(\d+) last=(\d+)$", text, re.M)
check("two complete snapshots", len(summary) == 2)
cfg = []
for expected, row in enumerate(summary):
    n, attempted, complete, error, reason, valid, c0, c1, started, last = map(number, row)
    check(f"snapshot{n} valid and complete", (n, attempted, complete, error, reason, valid) == (expected, 1, 1, 0, expected+1, 3) and started <= last)
    check(f"snapshot{n} RX/TX enabled and idle", all(c & 5 == 5 and c & 10 == 0 for c in (c0, c1)))
    check(f"snapshot{n} 16byte LE 32bit descriptors", all(c & ((1<<7)|(1<<8)|(1<<26)|(1<<29)) == 0 for c in (c0, c1)))
    check(f"snapshot{n} correct omitRX DMA distinction", not c0 & (1<<27) and bool(c1 & (1<<27)))
    cfg.append([c0, c1])
check("global configuration stable", cfg[0] == cfg[1])

pattern = r"^NORD2 WIFI RXDMAQUEUE: n=(\d+) slot=(\d+) q=(\d+) next=(\d+) valid=(\S+) base=(\S+) expected=(\S+) control=(\S+)/(\S+)/(\S+)/(\S+) ext_addr=(\S+) ext=(\S+) descriptor=(\S+) raw=(\S+)/(\S+)/(\S+)/(\S+)$"
rows = [[number(v) for v in row] for row in re.findall(pattern, text, re.M)]
check("eight complete ring snapshots", len(rows) == 8)
queues = [0, 2, 3, 4]
bases = [0x9002c000, 0x90030000, 0x90034000, 0x90034100]
buffers = [0x90035000, 0x90435000, 0x90835000, 0x90845000]
for ordinal, row in enumerate(rows):
    n, slot, q, nxt, valid, reg, expected, base, count, cidx, didx, ext_reg, ext, desc, sdp, control, w2, w3 = row
    count_expected = 1024 if slot < 2 else 16
    check(f"snapshot{n}q{q} exact mapping", (n, slot, q) == (ordinal//4, ordinal%4, queues[ordinal%4]))
    check(f"snapshot{n}q{q} owned layout and source count", (nxt, valid, reg, expected, base, count, cidx, didx, desc) == (0, 63, 0x18024500+16*slot, bases[slot], bases[slot], count_expected, count_expected-1, 0, bases[slot]))
    check(f"snapshot{n}q{q} final prefetch extent", (ext_reg, ext) == (0x18024680+4*slot, 4+0x400000*slot))
    check(f"snapshot{n}q{q} unchanged owned RX descriptor", (sdp, control, w2, w3) == (buffers[slot], 3840<<16, 0, 0))
check("all four ring snapshots stable", all(rows[i][1:] == rows[i+4][1:] for i in range(4)))

events = re.findall(r"^NORD2 WIFI RXEVENT: (.*)$", text, re.M)
check("14 control events captured", len(events) == 14)
check("13 sleepy and one scan-done", sum("eid=0x7 " in e and "kind=3 " in e for e in events) == 13 and sum("eid=0xd " in e and "kind=1 " in e for e in events) == 1)
check("no host RX drop or decoder rejection", "packets=14 rearmed=14 dropped=0 malformed=0 events=14 debug=0 sleepy=13 unknown=0 msdu=0 reports=0" in text)
check("matching scan completion with no BSS frames", "done=1 timed_out=0 partial=0 bss=0 frames=0 dropped=0 state=7 completed_channels=22" in text)
rf = re.findall(r"^NORD2 WIFI SCANRFCHANNEL: n=(\d+) channel=(\d+) beacon_probe=(\d+) mdrdy=(\d+) idle_raw=(\d+) scan_time_raw=(\d+)$", text, re.M)
check("22 firmware channel counter records", len(rf) == 22)
check("firmware counters zero beacons and only one MDRDY", all(int(r[2]) == 0 for r in rf) and sum(int(r[3]) for r in rf) == 1)

pins = {str(RECEIPT.relative_to(ROOT)): sha(RECEIPT)}
pins.update({str((ANDROID/f).relative_to(ROOT.parent)): sha(ANDROID/f) for f in source_files})
for f in ("wifi_main_init_plan.c", "wifi_main_initializer.c", "wifi_normal_rx.c", "wifi_rx_readback.c"):
    p = ROOT / "shim/nord2-xnu" / f
    pins[str(p.relative_to(ROOT))] = sha(p)
report = {
    "verdict": "NO_PROVEN_DMA_CORRECTION", "receipt_sha256": EXPECTED_SHA,
    "checks": checks, "check_count": len(checks), "pins": pins,
    "global_configuration": {"dma0": hex(cfg[0][0]), "dma1": hex(cfg[0][1]), "stable": True},
    "conclusions": [
        "All four DMA0 rings are configured at selected-source hardware offsets, source counts/CIDX, final manual-prefetch values, and software-owned descriptor/buffer addresses.",
        "Before and after the scan, all four DMA0 DIDX values remain zero and current descriptors retain DDONE0, SDL0=3840. No captured receive delivery or host decoder loss is demonstrated.",
        "DMA1 control events work: thirteen unsolicited sleepy notifications and matching scan-done. This does not prove RF/data delivery.",
        "Generic union PFET2 bit21 is reserved/RO in exact SoC3 generated register specification; its readback zero is not a missing setting.",
        "Bit20 loopback selector is set and preserved by both source and native RMW policy. Generated description is conditional on TXFIFO loopback traffic. No source-backed justification to clear it or attribute RF silence to it.",
        "Prior all-buffers3840 source-parity wording was too broad: selected ordinary DATA0 uses2352, while DATA1/event/reports use3840. Native DATA0 has an owned3840 capacity; larger capacity alone does not explain no DIDX movement and firmware beacon counts zero.",
        "The executed zero-V2-BSSID correction did not resolve the empty scan. No additional DMA write proposed. Pursue independently source-proven LinuxUP receive-filter/lifecycle gap with current bounded snapshots retained."
    ],
    "limits": [
        "Snapshots observe only two times and the current software-owned cell, not every DMA transaction or internal MAC state.",
        "No native IRQ delivery is enabled in the deliberately polling transport; transfer enable is present. Lack of IRQ output is not established as a DMA blocker.",
        "Firmware SCANRF interpretation and calibration/radio analysis belong to peer report; host counters alone cannot prove radio health.",
        "No source-level driver filter command ACK, firmware handler, or effect is claimed. No canonical, device, CI, gate or git changes."
    ]
}
(HERE / "report.json").write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps({"checks": len(checks), "verdict": report["verdict"], "report_sha256": sha(HERE / "report.json")}))
