#!/usr/bin/env python3
"""Strict offline ROM→pool→main-FW evidence. FW_READY is not networking."""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import re

P = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("dma_v2", P / "dma-v2-evidence.py")
dma = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dma)
U = r"(0x[0-9a-fA-F]+|[0-9]+)"
S = r"(-?(?:0x[0-9a-fA-F]+|[0-9]+))"


def pattern(prefix, fields, signed=()):
    return re.compile(re.escape(prefix) + " " + " ".join(
        re.escape(name) + "=" + (S if name in signed else U)
        for name in fields.split()) + r"$")


SCALARS = {
    "RESULT": ("NORD2 WIFI MAIN:", "status stage ready halt error events finish", ("status", "error")),
    "OWNER": ("NORD2 WIFI MAIN OWNER:", "ap wf driver dma asleep balanced native_halt transport_halt", ()),
    "NATIVE": ("NORD2 WIFI MAIN NATIVE:", "stage error halt live clock irq gic ap wf driver", ()),
    "PHY": ("NORD2 WIFI MAIN PHY:", "attempted event backup halt rx fields eid response status", ()),
    "PHYBACKUP": ("NORD2 WIFI MAIN PHYBACKUP:", "valid offset bytes copied reads halt error", ("error",)),
}
COMPOSITES = {
    "INIT": re.compile(r"NORD2 WIFI MAIN INIT: phase=" + U + " error=" + U + " halt=" + U +
                       " dirty=" + U + " sched=" + U + "/" + U + "/" + U +
                       " reg=" + U + " memory=" + U + " idle=" + U + r"$"),
    "IRQ": re.compile(r"NORD2 WIFI MAIN IRQ: ena0=" + U + "/" + U + " ena1=" + U + "/" + U +
                      " stat0=" + U + "/" + U + " stat1=" + U + "/" + U + " valid=" + U + "/" + U + r"$"),
    "TRANSPORT": re.compile(r"NORD2 WIFI MAIN TRANSPORT: error=" + U + " halt=" + U + " submit=" + U +
                            " events=" + U + " finish=" + U + " polls=" + U + " indices=" + U + "/" + U +
                            " bytes=" + U + "/" + U + "/" + U + r"$"),
    "DMASTATUS": re.compile(r"NORD2 WIFI MAIN DMASTATUS: raw=" + U + "/" + U + " valid=" + U + r"$"),
    "SPANS": re.compile(r"NORD2 WIFI MAIN SPANS: stage=" + U + " completed=" + U + " next=" + U +
                        " dirty=" + U + " halt=" + U + " words=" + U + "/" + U + " remaps=" + U +
                        " barriers=" + U + " error=" + S + r"$"),
    "SPANOBS": re.compile(r"NORD2 WIFI MAIN SPANOBS: target=" + U + " address=" + U + " expected=" + U +
                          " observed=" + U + " ready=" + U + " raw=" + U + " polls=" + U + r"$"),
}
ARM = re.compile(r"NORD2 WIFI MAIN ARM: stage=" + U +
                 r" action=([a-z0-9-]+) events=" + U + " finish=" + U +
                 " ap=" + U + " wf=" + U + " driver=" + U + r"$")
SPAN_ARM = re.compile(r"NORD2 WIFI MAIN SPAN ARM: index=" + U + " stage=" + U +
                      " finish=" + U + " events=" + U + " coex=" + U + r"$")
PCCIF = re.compile(r"NORD2 WIFI MAIN PCCIF ARM: addr=" + U +
                   " first=0 second=1 cal_emi=" + U + "/" + U + r"$")
FINISH = re.compile(r"NORD2 WIFI MAIN FINISH ARM: spans=" + U +
                    " cal=" + U + " coex=" + U + " cid=" + U + r"$")
ACTIONS = ("prepare", "fresh-wake-and-wfdma", "patch-copy", "patch-finish",
           "force-calibration", "pccif", "n9-copy", "n9-finish-and-ready")


def number(value):
    if len(value) > 22:
        return 1 << 65
    return int(value, 16 if value.lstrip("-").startswith("0x") else 10)


def parse(data):
    if len(data) > 8 * 1024 * 1024:
        return {"complete": False, "issues": ["input>8MiB"], "fw_ready": False,
                "scan_ready": False, "network_ready": False}
    issues, records, arms, spans, pccif, finish, kept = [], {}, [], [], [], [], []
    try:
        text = data.decode("utf8")
    except UnicodeDecodeError:
        text = data.decode("utf8", errors="replace")
        issues.append("invalid UTF8")
    patterns = {k: (pattern(pre, fields, signed), fields.split())
                for k, (pre, fields, signed) in SCALARS.items()}
    for line_number, line in enumerate(text.splitlines(keepends=True), 1):
        if "NORD2 WIFI MAIN" not in line:
            kept.append(line)
            continue
        body = line[line.index("NORD2 WIFI MAIN"):].rstrip("\r\n")
        if not line.endswith("\n") or len(body.encode()) >= 255 or line.count("NORD2 WIFI") != 1:
            issues.append("truncated/interleaved main report")
            continue
        found = False
        for key, (regex, names) in patterns.items():
            match = regex.fullmatch(body)
            if match:
                if key in records:
                    issues.append("duplicate " + key)
                records[key] = (line_number, dict(zip(names, map(number, match.groups()))))
                found = True
                break
        if found:
            continue
        for key, regex in COMPOSITES.items():
            match = regex.fullmatch(body)
            if match:
                if key in records:
                    issues.append("duplicate " + key)
                records[key] = (line_number, tuple(map(number, match.groups())))
                found = True
                break
        if found:
            continue
        match = ARM.fullmatch(body)
        if match:
            arms.append((line_number, (number(match[1]), match[2]) + tuple(map(number, match.groups()[2:]))))
            continue
        match = SPAN_ARM.fullmatch(body)
        if match:
            spans.append((line_number, tuple(map(number, match.groups()))))
            continue
        match = PCCIF.fullmatch(body)
        if match:
            pccif.append((line_number, tuple(map(number, match.groups()))))
            continue
        match = FINISH.fullmatch(body)
        if match:
            finish.append((line_number, tuple(map(number, match.groups()))))
            continue
        issues.append("unknown/refused/fatal main record")
    try:
        baseline = dma.parse("".join(kept).encode())
    except (ValueError, OverflowError, KeyError, IndexError):
        baseline = {"complete": False, "issues": ["malformed ROM/DMA baseline"]}
    issues += ["baseline: " + x for x in baseline["issues"]]
    if not baseline["complete"] or not baseline.get("pool_zero_verified"):
        issues.append("no same-boot verified ROM and region18 pool receipt")
    if set(records) != set(SCALARS) | set(COMPOSITES) or len(arms) != 8 or len(spans) != 7 or len(pccif) != 1 or len(finish) != 1:
        issues.append("missing/duplicate main report set")
    if set(records) == set(SCALARS) | set(COMPOSITES) and len(arms) == 8 and len(spans) == 7 and len(pccif) == 1 and len(finish) == 1:
        result = records["RESULT"][1]
        owner = records["OWNER"][1]
        native = records["NATIVE"][1]
        init = records["INIT"][1]
        phy = records["PHY"][1]
        backup = records["PHYBACKUP"][1]
        irq = records["IRQ"][1]
        transport = records["TRANSPORT"][1]
        dma_status = records["DMASTATUS"][1]
        placed = records["SPANS"][1]
        obs = records["SPANOBS"][1]
        if result != dict(status=0, stage=9, ready=1, halt=0, error=0, events=3, finish=2):
            issues.append("main result is not bounded FW_READY success")
        if owner != dict(ap=1, wf=1, driver=1, dma=1, asleep=0, balanced=0,
                         native_halt=0, transport_halt=0):
            issues.append("driver/DMA/awake ownership contradiction")
        if native["stage"] != 6 or native["error"] or native["halt"] or native["live"] != 31 or native["ap"] != 1 or native["wf"] != 1 or native["driver"] != 1 or native["gic"] & ((1 << 23) | (1 << 26)) or native["clock"] & 6 != 6 or not native["irq"] & 1:
            issues.append("fresh radio native receipt contradiction")
        if init[:4] != (5, 0, 0, 1) or init[4:7] != (1, 99, 0) or init[7] not in (91, 92) or init[8] != 2123 or not 2 <= init[9] <= 204:
            issues.append("nine-ring WFDMA initializer incomplete")
        if (irq[0] | irq[1]) & 0x33f800ff or (irq[2] | irq[3]) & 0x7d37ffff or (irq[4] | irq[5] | irq[6] | irq[7]) & 0x00300000 or irq[8:] != (3, 3):
            issues.append("peripheral IRQ/coherent fault contradiction")
        if transport[:5] != (0, 0, 0, 3, 2) or not 0 < transport[5] <= 6000 or transport[6:8] != (3, 3) or not 34 <= transport[8] <= 3840 or transport[9] > transport[8]:
            issues.append("actual descriptor-bounded command/event receipt incomplete")
        if dma_status[2] != 3 or (dma_status[0] | dma_status[1]) & 0x00300000:
            issues.append("transport coherent status invalid")
        if placed != (8, 127, 7, 1, 0, 244752, 244752, 4, 18, 0):
            issues.append("seven exact firmware spans/readbacks incomplete")
        last_address = 0x8fec3800 + 257520 - 4
        if obs[0] != 0x00404000 or obs[1] != last_address or obs[2] != obs[3] or obs[4] != 1 or obs[5] & 3 != 3 or not 1 <= obs[6] <= 500:
            issues.append("final span/FW_READY poll contradiction")
        if phy != dict(attempted=1, event=1, backup=1, halt=0, rx=60,
                       fields=1, eid=5, response=0x81, status=0):
            issues.append("actual FORCE_CAL event semantic receipt invalid")
        if not backup["valid"] or backup["halt"] or backup["error"] or backup["bytes"] == 0 or backup["offset"] + backup["bytes"] > 0x450000 or backup["copied"] != backup["bytes"] or backup["reads"] != (backup["bytes"] + 4095) // 4096:
            issues.append("firmware-reported EMI backup incomplete")
        if pccif[0][1] != (0x10003314, backup["offset"], backup["bytes"]) or finish[0][1] != (127, 1, 1, 0x41):
            issues.append("coexistence/finish request mismatch")
        expected_arm = [(i + 1, ACTIONS[i], *(((0, 0) if i < 4 else (1, 1) if i == 4 else (2, 1))),
                         *( (0, 0, 0) if i < 2 else (1, 1, 1))) for i in range(8)]
        if [entry for _, entry in arms] != expected_arm:
            issues.append("main stage/action/event/owner sequence")
        expected_span = [(i, i, 0 if i < 2 else 1, 0 if i < 2 else 2, 0 if i < 2 else 1)
                         for i in range(7)]
        if [entry for _, entry in spans] != expected_span:
            issues.append("patch/CAL/N9 span ownership sequence")
        order = ([arms[0][0], arms[1][0], arms[2][0]] + [x[0] for x in spans[:2]] +
                 [arms[3][0], arms[4][0], arms[5][0], pccif[0][0], arms[6][0]] +
                 [x[0] for x in spans[2:]] + [arms[7][0], finish[0][0]] +
                 [records[x][0] for x in ("RESULT", "OWNER", "NATIVE", "INIT", "IRQ",
                                           "TRANSPORT", "DMASTATUS", "SPANS", "SPANOBS", "PHY", "PHYBACKUP")])
        dma_tail = max((i for i, line in enumerate(text.splitlines(), 1) if "NORD2 WIFI DMA RING:" in line), default=0)
        outer = next((i for i, line in enumerate(text.splitlines(), 1)
                      if "NORD2 WIFI: power probe stage=" in line), 0)
        if not dma_tail or not outer or not dma_tail < order[0] or not order[-1] < outer or any(a >= b for a, b in zip(order, order[1:])):
            issues.append("same-boot source order or final outer report invalid")
        for key, (_, values) in records.items():
            if isinstance(values, dict):
                signed = set(SCALARS[key][2])
                invalid = any(not (-(1 << 31) <= x < (1 << 31) if name in signed else
                                   0 <= x < (1 << 32)) for name, x in values.items())
            else:
                invalid = any(not (-(1 << 31) <= x < (1 << 31) if key == "SPANS" and i == 9 else
                                   0 <= x < (1 << 32)) for i, x in enumerate(values))
            if invalid:
                issues.append("numeric width in " + key)
        if any(not 0 <= x < (1 << 32) for _, row in arms for x in (row[0],) + row[2:]) or any(
                not 0 <= x < (1 << 32) for _, row in spans for x in row) or any(
                not 0 <= x < (1 << 32) for entries in (pccif, finish) for _, row in entries for x in row):
            issues.append("numeric width in armed operations")
    baseline.update(complete=not issues, issues=issues,
                    classification="main_fw_ready" if not issues else "incomplete_or_conflicting",
                    fw_ready=not issues, scan_ready=False, network_ready=False,
                    dma_enabled=not issues, rf_calibration_command_executed=not issues,
                    main_records=records, main_arm=arms, main_span_arm=spans,
                    sha256=hashlib.sha256(data).hexdigest())
    return baseline


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("log", type=Path)
    args = ap.parse_args()
    result = parse(args.log.read_bytes())
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result["complete"] else 2)
