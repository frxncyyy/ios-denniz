#!/usr/bin/env python3
"""Private 08e6 STA-record BMC parity candidate (authored, NOT executed).

Authoritative candidate definition. Each edit is an exact string replacement
on a COPY of canonical shim/nord2-xnu that must match exactly once; any drift
from baseline b441429316787f81d62106e7a9892f282b28e174 fails loudly instead
of applying fuzzily. Never run this against the canonical tree in place.

Source basis (selected gen4m 04d00c2565764c3922c9b4826b5acff1106e533f):
  mgmt/cnm_mem.c:1141-1142  cnmStaSendUpdateCmd: ucWlanIndex = prStaRec->ucWlanIndex;
                            ucBMCWlanIndex = WTBL_RESERVED_ENTRY;   (unconditional)
  include/mgmt/privacy.h:115-119 WTBL_RESERVED_ENTRY == 255 on both branches
  nic/nic.c:1958-1969       BSS-info ucBMCWlanIndex keeps the real BMC mapping
Only the STA-record field changes. BSS-info BMC, GTK slot mapping, keys, TXD,
TXP, rings and every gate are untouched.

Usage: python3 -I edits.py --shim <canonical shim/nord2-xnu> --out <empty dir>
"""
from pathlib import Path
import argparse, hashlib, json, shutil

EDITS = [
    ("wifi_station_wire.h",
     "struct sw_sta {\n    uint8_t bss, sta, wlan, bmc_wlan, sta_type, state, need_response;\n",
     "/* Selected cnmStaSendUpdateCmd (cnm_mem.c1141-1142) sends the peer WLAN and\n"
     " * unconditionally WTBL_RESERVED_ENTRY(255) as STA-record ucBMCWlanIndex.\n"
     " * BSS-info ucBMCWlanIndex is a different field and keeps the real BMC slot. */\n"
     "#define SW_WTBL_RESERVED_ENTRY 255\n"
     "struct sw_sta {\n    uint8_t bss, sta, wlan, bmc_wlan, sta_type, state, need_response; /* bmc_wlan==SW_WTBL_RESERVED_ENTRY */\n"),
    ("wifi_station_wire.c",
     "p->aid>2007||!p->listen_interval||p->wlan==255||p->version>1||",
     "p->aid>2007||!p->listen_interval||p->wlan==255||p->bmc_wlan!=SW_WTBL_RESERVED_ENTRY||p->version>1||"),
    ("wifi_station_wire.c",
     "P8(CMD_UPDATE_STA_RECORD,ucBMCWlanIndex,p->bmc_wlan);",
     "P8(CMD_UPDATE_STA_RECORD,ucBMCWlanIndex,SW_WTBL_RESERVED_ENTRY);"),
    ("wifi_station_transport.c",
     " struct sw_sta a={.bss=s->peer.bss,.sta=s->peer.sta,.wlan=s->peer.wlan,.bmc_wlan=s->peer.group_wlan,.sta_type=65,",
     " /* cnm_mem.c1141-1142: STA-record BMC is WTBL_RESERVED_ENTRY; BSS b above keeps real BMC. */\n"
     " struct sw_sta a={.bss=s->peer.bss,.sta=s->peer.sta,.wlan=s->peer.wlan,.bmc_wlan=SW_WTBL_RESERVED_ENTRY,.sta_type=65,"),
]
# Invariants that must still hold after editing (guards against over-broad change).
KEEP = [
    ("wifi_station_wire.c", "P8(CMD_SET_BSS_INFO,ucPhyTypeSet,p->phy_set);P8(CMD_SET_BSS_INFO,ucBMCWlanIndex,p->bmc_wlan);"),
    ("wifi_station_transport.c", ".ap_sta=s->peer.sta,.bmc_wlan=s->peer.group_wlan,.nonht_phy="),
    ("wifi_station_transport.c", "if(!s->gtk_mapped)return s->peer.group_wlan;"),
    ("wifi_station_transport.c", "if(s->wire_cid==0x13&&s->wire[64+20]==3&&s->wire[64+51])"),
]

def apply(shim: Path, out: Path):
    out.mkdir(parents=True, exist_ok=True)
    for f in shim.iterdir():
        if f.suffix in ('.c', '.h', '.inc'):
            shutil.copyfile(f, out / f.name)
    changed = {}
    for name, old, new in EDITS:
        p = out / name
        text = p.read_text()
        n = text.count(old)
        if n != 1:
            raise SystemExit(f"edit anchor in {name} matched {n} times (expected 1); baseline drift")
        p.write_text(text.replace(old, new))
        changed[name] = True
    for name, needle in KEEP:
        if (out / name).read_text().count(needle) != 1:
            raise SystemExit(f"invariant lost in {name}: {needle[:60]}")
    if (out / "wifi_station_transport.c").read_text().count(".bmc_wlan=s->peer.group_wlan") != 1:
        raise SystemExit("exactly one group_wlan BMC initializer (BSS) must remain")
    return sorted(changed)

def main():
    a = argparse.ArgumentParser()
    a.add_argument('--shim', type=Path, required=True)
    a.add_argument('--out', type=Path, required=True)
    a.add_argument('--overlay', type=Path, help='optional dir receiving only changed files (for existing --candidate runners)')
    v = a.parse_args()
    if v.out.exists() and any(v.out.iterdir()):
        raise SystemExit('--out must be empty')
    changed = apply(v.shim, v.out)
    if v.overlay:
        v.overlay.mkdir(parents=True, exist_ok=True)
        for n in changed:
            shutil.copyfile(v.out / n, v.overlay / n)
    print(json.dumps({'changed': {n: hashlib.sha256((v.out / n).read_bytes()).hexdigest() for n in changed},
                      'baseline_sha256': {n: hashlib.sha256((v.shim / n).read_bytes()).hexdigest() for n in changed}}, indent=2))

if __name__ == '__main__':
    main()
