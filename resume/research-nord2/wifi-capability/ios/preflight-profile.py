#!/usr/bin/env python3
"""Offline raw-array check for the reviewed count-0/count-512 preflight policy.

This only recomputes the 112-read WFDMA profile. It does not assert a pool
receipt, transport initialization, firmware readiness, or a hardware pass.
"""
from pathlib import Path
import argparse
import json
import re

N = r"(0x[0-9a-fA-F]+|[0-9]+)"
CFG = re.compile(r"NORD2 WIFI DMA CFG: before=" + N + "/" + N + " after=" + N + "/" + N + " valid=" + N + r"$")
RING = re.compile(r"NORD2 WIFI DMA RING: n=" + N + " addr=" + N + " raw=" + N + "/" + N + "/" + N + "/" + N + " valid=" + N + r"$")
RINGS = ([0x18024500 + 16 * i for i in range(8)]
         + [0x18025300 + 16 * i for i in list(range(6)) + list(range(8, 14)) + list(range(16, 20))]
         + [0x18025500 + 16 * i for i in range(3)])


def capture(text):
    cfg = None
    rows = []
    for line in text.splitlines():
        if "NORD2 WIFI DMA CFG:" in line:
            if cfg is not None:
                raise ValueError("duplicate CFG")
            m = CFG.search(line)
            if not m:
                raise ValueError("malformed CFG")
            cfg = tuple(int(x, 0) for x in m.groups())
        elif "NORD2 WIFI DMA RING:" in line:
            m = RING.search(line)
            if not m:
                raise ValueError("malformed RING")
            rows.append(tuple(int(x, 0) for x in m.groups()))
    if cfg is None or len(rows) != 27 or cfg[4] != 15:
        raise ValueError("incomplete 112-read preflight")
    if any(x >= 1 << 32 for x in cfg):
        raise ValueError("CFG overflow")
    for i, row in enumerate(rows):
        if row[0] != i or row[1] != RINGS[i] or row[6] != 15:
            raise ValueError("ring order/address/validity")
        if any(x >= 1 << 32 for x in row):
            raise ValueError("RING overflow")
    return cfg, rows


def classify(cfg, rows):
    if len(cfg) != 5 or len(rows) != 27:
        raise ValueError("incomplete capture")
    first = (0, 0, 0, 0)

    def mismatch(reason, address, value, mask):
        nonlocal first
        if first[0] == 0:
            first = (reason, address, value, mask)

    for i in range(2):
        if cfg[i] & 15:
            mismatch(1, 0x18024208 + 0x1000 * i, cfg[i], 15)
    for i, row in enumerate(rows):
        _, address, base, control, cidx, didx, _ = row
        if base:
            mismatch(2, address, base, 0xffffffff)
        defined = control & 0xf0fff
        if (defined & 0xfff) == 0x200:
            defined &= ~0xfff
        if defined:
            mismatch(2, address + 4, control, 0xf0fff)
        if cidx & 0xfff:
            mismatch(2, address + 8, cidx, 0xfff)
        if didx & 0xfff:
            mismatch(2, address + 12, didx, 0xfff)
    stable = True
    for i in range(2):
        if cfg[2 + i] & 15:
            mismatch(1, 0x18024208 + 0x1000 * i, cfg[2 + i], 15)
        if (cfg[i] ^ cfg[2 + i]) & 15:
            stable = False
            mismatch(3, 0x18024208 + 0x1000 * i, cfg[2 + i], 15)
    return {"idle": first[0] == 0, "stable": stable, "first": first,
            "reads": 112, "rings": 27, "dma_enabled": False,
            "pool_receipt_proven": False, "fw_ready": False,
            "scan_ready": False, "network_ready": False}


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("log", type=Path)
    args = ap.parse_args()
    print(json.dumps(classify(*capture(args.log.read_text())), indent=2))
