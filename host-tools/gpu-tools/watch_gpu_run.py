#!/usr/bin/env python3
"""Follow a live xnu-live.log and flag the abort criteria of a raised-OPP GPU run.

Prints one status line per NORD2 GPU JOBS record and an ABORT line (exit 3) on:
gpu_khz != expected or mux != 1, any fault/timeout, battery +4 C over the first
reading or >= 40 C, LVTS gpu/soc >= 70 C, or a NORD2 GPU BUDGET rejection storm.
"""
import argparse, re, sys, time
from pathlib import Path

KV = re.compile(r'(\w+)=(-?[0-9]+)')

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('log', type=Path)
    ap.add_argument('--khz', type=int, default=654000)
    ap.add_argument('--seconds', type=float, default=600)
    a = ap.parse_args()
    end = time.time() + a.seconds
    while not a.log.exists() and time.time() < end:
        time.sleep(1)
    pos, bat0, budget = 0, None, []
    while time.time() < end:
        data = a.log.read_bytes()[pos:]
        if not data:
            time.sleep(1); continue
        cut = data.rfind(b'\n') + 1
        pos += cut
        for raw in data[:cut].decode('utf-8', 'replace').splitlines():
            why = None
            if 'NORD2 GPU JOBS:' in raw:
                kv = {k: int(v) for k, v in KV.findall(raw)}
                bat = kv.get('bat_cC') if kv.get('bat_ok') == 1 else None
                if bat is not None and bat0 is None:
                    bat0 = bat
                print(f"khz={kv.get('gpu_khz')} mux={kv.get('mux')} bat={bat} gpu={kv.get('gpu_cC')} soc={kv.get('soc_cC')} fault={kv.get('fault')}", flush=True)
                # Console lines can be cut by interleaved writers: judge only fields present.
                # A cut can also leave a partial number ("gpu_khz=654|..."), so only
                # judge the clock when the field after it (mux) is intact too.
                clock = re.search(r'gpu_khz=(\d+) mux=(\d+)\b', raw)
                if clock and (int(clock.group(1)) != a.khz or int(clock.group(2)) != 1):
                    why = 'clock not at requested OPP'
                elif kv.get('fault', 0) > 0:
                    why = 'GPU fault'
                elif bat is not None and (bat >= 4000 or (bat0 is not None and bat - bat0 > 400)):
                    why = f'battery temperature {bat / 100:.1f} C (start {bat0 / 100:.1f} C)'
                elif max(kv.get('gpu_cC', -9999), kv.get('soc_cC', -9999)) >= 7000:
                    why = 'LVTS temperature >= 70 C'
            elif 'NORD2 GPU' in raw and (re.search(r'\b(fault|timeout)=[1-9]', raw) or
                                         re.search(r'\b(mmu|job|gpu) (fault|timeout)\b', raw, re.I)):
                why = 'fault/timeout line: ' + raw[:160]
            elif 'NORD2 GPU BUDGET' in raw:
                now = time.time(); budget = [t for t in budget if now - t < 10] + [now]
                if len(budget) >= 5:
                    why = 'budget rejection storm'
            if why:
                print('ABORT: ' + why, flush=True)
                return 3
    print('watch finished without abort', flush=True)
    return 0

if __name__ == '__main__':
    sys.exit(main())
