#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""gpu/hwcodec host tool for kernel patch 0311 (VENC bring-up, autotest command 8). The VENC twin of hwc.py (command 7).

  hwv.py run OUTDIR [--upto N] [--no-off] [--c1] [--reg OFF [--write VAL]]
        phone test: steps 0 (status, read only), 1 (VENC power + clocks + larb ports), 2 (larb read-back + interrupt status),
        3 (VENC registers), 4 (off).  The kernel log is saved after every step (OUTDIR/NN-stepS.log, OUTDIR/kernel.log).
        --upto 0 = read-only status only.  Stops at the first step that does not return 1 and then still runs step 4.
        --c1: VENC core 1 as well (VEN_CORE1 + larb8; needs the MDP domain on, else result 19 and nothing of core 1 is touched).
        --reg OFF: step 3 reads 16 words from VENC + OFF (default: only +0x5c); --write VAL also writes VAL at OFF, reads it
        back and restores the old value (only for a register known to be a plain read/write one).
  hwv.py step N OUTDIR [--c1] [--reg OFF [--write VAL]]     one step
  Clock (step 1, VEN off): since q10jx the driver's VENC power-on selects mainpll_d4_d2 = 273 MHz by default (Linux's lowest
        VENC step, lowest vcore OPP).  --fast selects univpll_d4 = 624 MHz (the phone's boot value) instead; --slow forces 273 MHz
        explicitly (the only way to get it on q10jw).  The log line "venc_sel A -> B" / CLK_CFG_13 shows what ran.
  hwv.py smoke OUTDIR --reg OFF --write VAL [--c1] [--slow]
        NOT NEEDED FOR THE FIRST TEST, ready for later: one-shot encode-path smoke = steps 0, 1, 2, then step 3 writing VAL to a
        PLAIN read/write VENC register at OFF, reading it back and restoring it, then step 4.  Refuses without --reg/--write:
        no such register is identified yet (opus-venc's facts/VENC_PROTOCOL.md will name one; until then do not guess).

Results: 1 done, 16 bad step/flags, 17 mapping, 18 display domain off (or a clock root gated), 19 MDP domain off (core 1),
20 power sequence failed (timeouts: see the log line), 21 not powered (run step 1), 22 bad arguments, 23 larb read-back wrong,
24 write read-back differs, 25 busy (--slow/--fast while VEN is on), 2 = this kernel has no command 8 (not a 0311 kernel).
"""
import argparse, struct, subprocess, sys, time
from pathlib import Path

HERE = Path(__file__).resolve().parent
AUTOTEST = HERE.parents[1] / 'build/autotest'
RESULTS = {1: 'done', 2: 'this kernel has no command 8 (not a 0311 kernel)', 16: 'bad step', 17: 'mapping failed',
           18: 'display domain off / disp_sel gated', 19: 'MDP domain off (core 1 not touched)', 20: 'power sequence failed',
           21: 'not powered (run step 1)', 22: 'bad arguments', 23: 'larb read-back wrong', 24: 'write read-back differs',
           25: 'busy'}


def phone():
    sys.path.insert(0, str(AUTOTEST))
    import phone as ph
    return ph, ph.Phone(wait=5.0)


def run_step(p, step, out, n, c1, reg, write, slow=False, fast=False):
    regs = (reg or 0, 1 if write is not None else 0, write or 0)
    if (write is not None and write > 0xffff):
        raise SystemExit('--write must fit 16 bits (the autotest registers are 16-bit wValue)')
    for i, v in enumerate(regs):
        p.reg(i, v)
    arg = step | (0x100 if c1 else 0) | (0x200 if slow and step == 1 else 0) | (0x400 if fast and step == 1 else 0)
    ok, token = struct.unpack('<2I', p.req(0x72, 8, arg, 8))
    if not ok:
        raise SystemExit('command 8 refused: the autotest thread is busy, or this is not a 0311 kernel')
    t_end = time.monotonic() + 20
    while True:
        s = p.status()
        if s['done'] == token and not s['busy']:
            break
        if time.monotonic() > t_end:
            raise SystemExit(f'step {step} did not finish in 20 s (kernel thread stuck?) - stop here, check `phone.py status`')
        time.sleep(0.01)
    time.sleep(0.3)
    log = out / f'{n:02d}-step{step}.log'
    subprocess.run([sys.executable, str(AUTOTEST / 'phone.py'), 'logs', str(log), '--seconds', '3'], capture_output=True)
    lines = [l for l in log.read_text(errors='replace').splitlines() if 'NORD2 VCODEC' in l] if log.exists() else []
    if log.exists():
        with (out / 'kernel.log').open('ab') as k:
            k.write(log.read_bytes())
    print(f'step {step}: result {s["result"]} ({RESULTS.get(s["result"], "?")})')
    for l in lines:
        print('   ', l[-260:])
    return s


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('verb', choices=['run', 'step', 'smoke']); ap.add_argument('args', nargs='+')
    ap.add_argument('--slow', action='store_true'); ap.add_argument('--fast', action='store_true')
    ap.add_argument('--upto', type=int, default=4); ap.add_argument('--no-off', action='store_true')
    ap.add_argument('--c1', action='store_true')
    ap.add_argument('--reg', type=lambda x: int(x, 0)); ap.add_argument('--write', type=lambda x: int(x, 0))
    a = ap.parse_args()
    if a.write is not None and not a.reg:
        raise SystemExit('--write needs --reg')
    if a.reg is not None and (a.reg & 3 or a.reg > 0x3000 - 64):
        raise SystemExit('--reg must be 4-aligned and <= 0x2fc0')
    if a.slow and a.fast:
        raise SystemExit('--slow and --fast exclude each other')
    if a.verb == 'smoke' and (a.reg is None or a.write is None):
        raise SystemExit('smoke needs --reg OFF --write VAL of a register known to be plain read/write (none identified yet)')
    if a.verb == 'step':
        steps, out = [int(a.args[0])], Path(a.args[1])
    else:
        steps, out = [s for s in (0, 1, 2, 3, 4) if s <= a.upto and not (s == 4 and a.no_off)], Path(a.args[0])
    out.mkdir(parents=True, exist_ok=True)
    ph, p = phone()
    n0 = len(list(out.glob('*-step*.log'))); rc = 0
    for n, step in enumerate(steps, n0):
        s = run_step(p, step, out, n, a.c1, a.reg if step == 3 else None, a.write if step == 3 else None, a.slow, a.fast)
        if s['result'] != 1:
            rc = 1
            if step not in (0, 4) and 4 in steps:
                print('stopping; step 4 (off) is still run to drop the autotest\'s references')
                run_step(p, 4, out, n + 1, a.c1, None, None)
            break
    p.close()
    return rc


if __name__ == '__main__':
    sys.exit(main())
