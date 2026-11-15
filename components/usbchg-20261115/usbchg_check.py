#!/usr/bin/env python3
"""usbchg-20261115: verdict for pieces 0700 (boot without USB host), 0701 (gauge/IOPMPowerSource), 0702 (MP2650 charge control).
usage: python3 -I usbchg_check.py XNU_LIVE_LOG [more logs...]
Prints the key lines and a verdict per piece.  Read-only."""
import re, sys

def main(paths):
    lines = []
    for p in paths:
        with open(p, 'rb') as f:
            lines += [l.decode('utf-8', 'replace').rstrip('\r\n') for l in f]
    pick = lambda pat: [l for l in lines if re.search(pat, l)]
    usb = pick(r'NORD2 USB14:|NORD2 USB: (clock gated|device inactive|background console ready|background initialization)')
    bat = pick(r'NORD2 BAT14:')
    chg = pick(r'NORD2 CHG14:')
    regs = pick(r'NORD2 CHG (REGS|CFG):')
    old = pick(r'NORD2 BATTERY:')
    print('== 0700 USB'); [print('  ' + l) for l in usb[:12]]
    if any('console up at boot' in l for l in usb): print('USB_CONSOLE_AT_BOOT (cable present at power-on)')
    if any('no usable USB at boot' in l for l in usb): print('USB_NO_HOST_BOOT_CONTINUED (0700 fix path taken; boot went on)')
    if any('late console up' in l for l in usb): print('USB_LATE_CONSOLE_UP')
    if any('NORD2-XNU: stop at after background MTU3' in l for l in lines): print('USB_STOP_POINT_HIT (old behaviour!)')
    print('== 0701 battery (%d BAT14 lines)' % len(bat))
    for l in bat[:3] + (['  ...'] if len(bat) > 6 else []) + bat[-3:]: print('  ' + l)
    shown = [(int(m.group(1)), int(m.group(2)), int(m.group(3))) for m in (re.search(r't=(\d+)s shown=(\d+) raw=(\d+)', l) for l in bat) if m]
    if shown:
        print('BAT_SOC first t=%ds shown=%d raw=%d  last t=%ds shown=%d raw=%d  (%d samples)' % (shown[0] + shown[-1] + (len(shown),)))
    stale = sum(1 for l in bat if 'stale=1' in l)
    print('BAT_STALE_LINES %d' % stale)
    ext = [l for l in bat if 'ios ext=1' in l]
    print('BAT_EXTERNAL_SEEN %s, BAT_CHARGING_SEEN %s' % (bool(ext), any('charging=1' in l.split('| ios')[-1] for l in bat)))
    curs = [int(m.group(1)) for m in (re.search(r' avg=(-?\d+)', l) for l in bat) if m]
    if curs: print('BAT_AVG_CURRENT mA min %d max %d last %d (+ = charging)' % (min(curs), max(curs), curs[-1]))
    edge = pick(r'NORD2 BAT15:')
    print('== 0703 plug edges (%d BAT15 lines)' % len(edge)); [print('  ' + l) for l in edge[:12]]
    lat = [int(m.group(1)) for m in (re.search(r'published pg=\d (\d+)ms after the edge', l) for l in edge) if m]
    if lat: print('EDGE_PUBLISH_MS first-refresh %d (max of all %d)' % (lat[0], max(lat)))
    print('== 0702 charger (%d CHG14 lines, %d REGS/CFG lines)' % (len(chg), len(regs)))
    for l in regs[:2] + chg[:8]: print('  ' + l)
    if len(chg) > 8: print('  ...'); [print('  ' + l) for l in chg[-4:]]
    fails = [l for l in chg if 'FAILED' in l or 'latched' in l]
    if fails: print('CHG_WRITE_FAILURES %d (first: %s)' % (len(fails), fails[0]))
    types = sorted(set(m.group(1) for m in (re.search(r'type=([a-z ]+?) pg=', l) for l in chg) if m))
    print('CHG_TYPES_SEEN %s' % types)
    if old and not bat: print('NOTE: only the pre-0701 NORD2 BATTERY line: not a q10jfinal14usb kernel?')

if __name__ == '__main__':
    if len(sys.argv) < 2: sys.exit(__doc__)
    main(sys.argv[1:])
