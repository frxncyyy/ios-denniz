#!/usr/bin/env python3
"""Synthetic future report over a historical ROM log; never hardware proof."""
from pathlib import Path
import importlib.util

P = Path(__file__).resolve().parent
ROOT = P.parents[3]
spec = importlib.util.spec_from_file_location("main_parse", P / "parse-evidence.py")
main = importlib.util.module_from_spec(spec)
spec.loader.exec_module(main)
rom = (ROOT / "research/nord2/wifi-rom/ios/hardware-live/xnu-live.log").read_text()
pool = [
    "NORD2 WIFI DMA PREP: status=0 claim=1 base=0x90000000 bytes=0xf20000 layout=0x20000",
    "NORD2 WIFI DMA ARM: stage=5 action=preflight base=0x18024000 rings=27",
    "NORD2 WIFI DMA SMC ARM: group=1 base=0x90000000 bytes=0xf20000 x0=0xc200050b x1=0 x2=0x12009000 x3=0x10090f1 x4=0xb6db6d",
    "NORD2 WIFI DMA SMC ARM: group=0 base=0x90000000 bytes=0xf20000 x0=0xc200050b x1=0 x2=0x12009000 x3=0x90f1 x4=0x80b6da28",
    "NORD2 WIFI DMA ARM: stage=8 action=map base=0x90000000 bytes=0xf20000",
    "NORD2 WIFI DMA ARM: stage=9 action=zero base=0x90000000 bytes=0x20000",
    "NORD2 WIFI DMA: status=0 stage=12 inherited=0 preflight=1 idle=1 stable=1 reads=112 halt=0 error=0",
    "NORD2 WIFI DMA SET: attempted=3 accepted=2 raw_valid=3 raw1=0 raw0=0 mapped=1 zero=1 verified=1 words=32768/32768",
    "NORD2 WIFI DMA RAW: group=1 x=0/0x100/0/0",
    "NORD2 WIFI DMA RAW: group=0 x=0/0x100/0/0",
    "NORD2 WIFI DMA OWNER: ap=0 wf=0 asleep=1 balanced=1 possible_lock=1 live=0x1f cfg=0x20010101 local=0x20010000 done=0x1d1e first=0/0/0/0",
    "NORD2 WIFI DMA LIVE: cpu=1 top=0x80 power=4 protection=0 dma_protection=0",
    "NORD2 WIFI DMA CFG: before=0x1010b870/0x1810b870 after=0x1010b870/0x1810b870 valid=0xf",
]
for i, address in enumerate(main.dma.RINGS):
    pool.append(f"NORD2 WIFI DMA RING: n={i} addr={address:#x} raw=0/0x200/0/0 valid=0xf")
arm = [(1, "prepare", 0, 0, 0, 0, 0),
       (2, "fresh-wake-and-wfdma", 0, 0, 0, 0, 0),
       (3, "patch-copy", 0, 0, 1, 1, 1),
       (4, "patch-finish", 0, 0, 1, 1, 1),
       (5, "force-calibration", 1, 1, 1, 1, 1),
       (6, "pccif", 2, 1, 1, 1, 1),
       (7, "n9-copy", 2, 1, 1, 1, 1),
       (8, "n9-finish-and-ready", 2, 1, 1, 1, 1)]
def arm_line(row):
    s, action, events, finish, ap, wf, driver = row
    return f"NORD2 WIFI MAIN ARM: stage={s} action={action} events={events} finish={finish} ap={ap} wf={wf} driver={driver}"
def span_line(i):
    return f"NORD2 WIFI MAIN SPAN ARM: index={i} stage={i} finish={int(i>=2)} events={0 if i<2 else 2} coex={int(i>=2)}"
workflow = [arm_line(arm[0]), arm_line(arm[1]), arm_line(arm[2]), span_line(0), span_line(1),
            arm_line(arm[3]), arm_line(arm[4]), arm_line(arm[5]),
            "NORD2 WIFI MAIN PCCIF ARM: addr=0x10003314 first=0 second=1 cal_emi=0x100/0x800",
            arm_line(arm[6])] + [span_line(i) for i in range(2, 7)] + [
            arm_line(arm[7]),
            "NORD2 WIFI MAIN FINISH ARM: spans=0x7f cal=1 coex=1 cid=0x41",
            "NORD2 WIFI MAIN: status=0 stage=9 ready=1 halt=0 error=0 events=3 finish=2",
            "NORD2 WIFI MAIN OWNER: ap=1 wf=1 driver=1 dma=1 asleep=0 balanced=0 native_halt=0 transport_halt=0",
            "NORD2 WIFI MAIN NATIVE: stage=6 error=0 halt=0 live=0x1f clock=0x4006 irq=1 gic=0 ap=1 wf=1 driver=1",
            "NORD2 WIFI MAIN INIT: phase=5 error=0 halt=0 dirty=1 sched=1/99/0 reg=91 memory=2123 idle=2",
            "NORD2 WIFI MAIN IRQ: ena0=0/0 ena1=0/0 stat0=0/0 stat1=0/0 valid=0x3/0x3",
            "NORD2 WIFI MAIN TRANSPORT: error=0 halt=0 submit=0 events=3 finish=2 polls=6 indices=3/3 bytes=36/36/12",
            "NORD2 WIFI MAIN DMASTATUS: raw=0/0 valid=0x3",
            "NORD2 WIFI MAIN SPANS: stage=8 completed=127 next=7 dirty=1 halt=0 words=244752/244752 remaps=4 barriers=18 error=0",
            f"NORD2 WIFI MAIN SPANOBS: target=0x404000 address={0x8fec3800+257520-4:#x} expected=0x1234 observed=0x1234 ready=1 raw=0x3 polls=37",
            "NORD2 WIFI MAIN PHY: attempted=1 event=1 backup=1 halt=0 rx=60 fields=1 eid=0x5 response=0x81 status=0",
            "NORD2 WIFI MAIN PHYBACKUP: valid=1 offset=0x100 bytes=2048 copied=2048 reads=1 halt=0 error=0"]
outer = rom.index("NORD2 WIFI: power probe stage=")
data = (rom[:outer] + "\n".join(pool + workflow) + "\n" + rom[outer:]).encode()
result = main.parse(data)
assert result["complete"] and result["classification"] == "main_fw_ready" and result["fw_ready"]
assert not result["scan_ready"] and not result["network_ready"], result["issues"]
for old, new in [
    (b"raw=0/0x200/0/0", b"raw=0/0x201/0/0"),
    (b"raw=0/0x200/0/0", b"raw=1/0x200/0/0"),
    (b"events=3 finish=2", b"events=2 finish=1"),
    (b"response=0x81 status=0", b"response=0x81 status=1"),
    (b"spans=0x7f cal=1", b"spans=0x7e cal=1"),
    (b"sched=1/99/0", b"sched=1/98/0"),
    (b"reg=91 memory=2123", b"reg=90 memory=2123"),
    (b"reg=91 memory=2123", b"reg=91 memory=2122"),
    (b"valid=0x3/0x3", b"valid=0x3/0x1"),
    (b"raw=0/0 valid=0x3", b"raw=0x100000/0 valid=0x3"),
    (b"ready=1 raw=0x3 polls=37", b"ready=1 raw=0x1 polls=37"),
    (b"NORD2 WIFI MAIN ARM: stage=8", b"NORD2 WIFI MAIN ARM: stage=7"),
]:
    altered = main.parse(data.replace(old, new, 1))
    assert not altered["complete"], (old, altered["issues"])
assert not main.parse(data + data)["complete"]
assert not main.parse(data[:-1] + b"\xff")["complete"]
assert main.parse(data.replace(b"reg=91 memory=2123", b"reg=92 memory=2123", 1))["complete"]
assert not main.parse(data.replace(b"gic=0 ap=1", b"gic=0x100000000 ap=1", 1))["complete"]
assert not main.parse(data.replace(b"NORD2 WIFI MAIN ARM: stage=3", b"NORD2 WIFI DMA NORD2 WIFI MAIN ARM: stage=3", 1))["complete"]
assert not main.parse(data.replace(b"NORD2 WIFI MAIN: status=0", b"NORD2 WIFI MAIN: status=18446744073709551616", 1))["complete"]
assert not main.parse(data.replace(b"NORD2 WIFI MAIN SPAN ARM: index=6", b"NORD2 WIFI MAIN SPAN ARM: index=60", 1))["complete"]
duplicate = b"NORD2 WIFI MAIN PHY: attempted=1 event=1 backup=1 halt=0 rx=60 fields=1 eid=0x5 response=0x81 status=0\n"
assert not main.parse(data.replace(duplicate, duplicate + duplicate, 1))["complete"]
assert not main.parse(data.replace(b"NORD2 WIFI MAIN PHYBACKUP:", b"NORD2 WIFI MAIN PHYBACKUP: " + b"9" * 5000, 1))["complete"]
print("PASS synthetic count512 pool plus exact main ordering/receipts; malformed/partial/coherent faults refused; no hardware")
