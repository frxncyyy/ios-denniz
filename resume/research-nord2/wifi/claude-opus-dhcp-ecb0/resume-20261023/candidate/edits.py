#!/usr/bin/env python3
"""Private OFLD-routing candidate (authored, NOT executed).

Hypothesis-level, security-preserving correction to the native RX error gate.
Exact string replacement on a COPY of canonical shim/nord2-xnu; the anchor must
match exactly once or the transform aborts (guards against baseline drift from
b5a09a265bdd96b34019959aa03e67fb842a6af1).

Source basis (selected gen4m 04d00c2565764c3922c9b4826b5acff1106e533f):
  nic/nic_rx.c:3661-3688  nicRxProcessPacketType: "OFLD pkts should go data
     flow / 1:EAPOL 2:ARP/NS 3:TDLS"; if (ucOFLD || fgHdrTran) -> data path.
  nic/nic_rxd_v2.c:363-514 nic_rxd_v2_sanity_check drops ONLY on FCS/DAF/ICV/
     TKIP-MIC. OFLD is never a drop condition.
  include/nic/nic_connac2x_rx.h:171-172 OFLD = DW4 BITS(11,12); :140 DW2 bit25
     = TRANS_FAIL (not DW1 bit25 ICV).

Canonical native wifi_station_transport.c:211 additionally drops any nonzero
OFLD (d4 & (3u<<11)) before the secure/header checks. That is stricter than the
selected source. The two observed unicast-to-me (UC2ME) CCMP records in
pause-final were rejected here on OFLD alone (d1=d2=0, PF=0).

This change removes ONLY the OFLD term from the hard-drop condition and instead
emits the existing numeric RXERROR breadcrumb and lets the frame fall through to
the UNCHANGED secure decode (CCMP sec==4, per-index key match, PN/replay, peer/
self address, controlled port, length bounds). Every other error bit (cipher/
ICV/FCS in d1, TRANS_FAIL/EXCEED/DE_AMSDU/FRAG in d2, PF-status in d4) still
drops exactly as before.

NOT PROVEN to deliver DHCP: source labels OFLD=3 as TDLS and the EtherType/
payload was not captured, so these frames may not be the DHCP reply. The
retained RXERROR records plus delivery counters make the next capture
self-discriminating.

Usage: python3 -I edits.py --shim <canonical shim/nord2-xnu> --out <empty dir>
"""
from pathlib import Path
import argparse, hashlib, json, shutil

ANCHOR = ("if(d1&0x0f800000u||d2&0x0b800000u||(d4&3)||(d4&(3u<<11)))"
          "{nsdd_rx_error(&s->data_diag,s->generation,p,n);"
          "nsdd_reject(&s->data_diag,s->generation,NSDD_RX_ERROR);return 0;}")
REPLACEMENT = ("if(d1&0x0f800000u||d2&0x0b800000u||(d4&3))"
               "{nsdd_rx_error(&s->data_diag,s->generation,p,n);"
               "nsdd_reject(&s->data_diag,s->generation,NSDD_RX_ERROR);return 0;}\n"
               " /* OFLD (DW4[11:12]) is routing metadata, not an error: selected gen4m"
               " nic_rx.c:3661-3688 sends OFLD/translated frames to the data flow and"
               " nic_rxd_v2_sanity_check never drops on OFLD. Observe numerically, then"
               " fall through to the UNCHANGED secure decode (CCMP/key/replay/peer/port/"
               " length still enforced below). Hypothesis correction; not proven DHCP. */\n"
               " if(d4&(3u<<11))nsdd_rx_error(&s->data_diag,s->generation,p,n);")

# After the edit these secure-decode gates MUST still be present exactly once each.
KEEP = [
    "nsdd_reject(&s->data_diag,s->generation,NSDD_RX_CIPHER);return 0;",
    "nsdd_reject(&s->data_diag,s->generation,NSDD_RX_KEY);return 0;",
    "nsdd_reject(&s->data_diag,s->generation,NSDD_RX_REPLAY);return 0;",
    "nsdd_reject(&s->data_diag,s->generation,NSDD_RX_PORT);return 0;",
    "nsdd_reject(&s->data_diag,s->generation,NSDD_RX_PEER);return 0;",
    "nsdd_reject(&s->data_diag,s->generation,NSDD_RX_FC);return 0;",
]

def apply(shim: Path, out: Path):
    out.mkdir(parents=True, exist_ok=True)
    for f in shim.iterdir():
        if f.suffix in ('.c', '.h', '.inc'):
            shutil.copyfile(f, out / f.name)
    p = out / 'wifi_station_transport.c'
    text = p.read_text()
    if text.count(ANCHOR) != 1:
        raise SystemExit(f"RX error-gate anchor matched {text.count(ANCHOR)} times (expected 1); baseline drift")
    text = text.replace(ANCHOR, REPLACEMENT)
    # OFLD must no longer be a hard-drop term, but still observed and still one gate line.
    if "(d4&(3u<<11))){nsdd_rx_error" in text:
        raise SystemExit("OFLD still in hard-drop condition")
    if text.count("if(d4&(3u<<11))nsdd_rx_error(&s->data_diag,s->generation,p,n);") != 1:
        raise SystemExit("OFLD observational branch not uniquely present")
    p.write_text(text)
    for needle in KEEP:
        if text.count(needle) != 1:
            raise SystemExit(f"secure-decode gate lost or duplicated: {needle[:40]}")
    # d1/d2/PF error terms preserved.
    if text.count("if(d1&0x0f800000u||d2&0x0b800000u||(d4&3))") != 1:
        raise SystemExit("hard-error terms (cipher/ICV/FCS/TRANS_FAIL/PF) not preserved")
    return ['wifi_station_transport.c']

def main():
    a = argparse.ArgumentParser()
    a.add_argument('--shim', type=Path, required=True)
    a.add_argument('--out', type=Path, required=True)
    a.add_argument('--overlay', type=Path, help='optional dir receiving only changed files')
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
