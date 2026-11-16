#!/usr/bin/env python3
"""bt_check.py XNU_LIVE_LOG : stage-by-stage verdict of the Bluetooth bring-up (pieces 0720-0723) from a USB kernel log.
Prints one line per stage (PASS / FAIL / not reached) and the first failure detail. Read-only."""
import re, sys

STAGES = [
    ('daemon started (n2btd, payload bt1)', r'NORD2 BT U: n2btd starts'),
    ('address stored/loaded', r'NORD2 BT0: address from the daemon: ([0-9a-f:]{17})'),
    ('stage set', r'NORD2 BT U: stage: stop after (S\d+)'),
    ('waited for uptime/Wi-Fi', r'NORD2 BT U: starting at uptime (\d+) s'),
    ('guard written', r'NORD2 BT U: guard written'),
    ('bring-up requested', r'NORD2 BT0: bring-up \d+ requested'),
    ('breadcrumb zone mapped', r'NORD2 BT0: \d+ \d+ zone 0x4811e000 previous boot'),
    ('S1 conninfra on (AP side, 3 reads)', r'NORD2 BT0: \d+ \d+ S1 (ok x3|FORCED)'),
    ('stopped by kern.n2bt_maxstep', r'NORD2 BT0: \d+ \d+ (stopped-at-S\d+(-kept)?)'),
    ('fw own handed back (bt3)', r'NORD2 BT0: \d+ \d+ end: fw own (acknowledged|NOT acknowledged)'),
    ('quiesce check', r'NORD2 BT0: \d+ \d+ (quiesce [a-z-]+: GPS[^\n]*)'),
    ('settled (post-stop monitor done)', r'NORD2 BT0: \d+ \d+ ([^ ]*:settled)'),
    ('S4 firmware files read', r'NORD2 BT0: S4 /usr/share/firmware/nord2-bt/soc3_0_ram_bt_1a_1_hdr.bin: 0 '),
    ('S2 wake vote + MT6893 id + remap', r'NORD2 BT0: S2 cfg id 0x20010101 remap mcu_emi 0x8fb0'),
    ('S4 firmware placed in EMI', r'NORD2 BT0: S4 soc3_0_ram_bt_1a_1_hdr.bin: built'),
    ('S5 BGF ids', r'NORD2 BT0: S5 BGF ids hw 0x8a00 fw 0x8a00 code 0 ip 0x20010000'),
    ('S5 BGF MCU ROM idle 0x1D1E', r'NORD2 BT0: S5 MCU ROM idle'),
    ('S6 BTIF on', r'NORD2 BT0: S6 BTIF on'),
    ('S7 WMT func on', r'NORD2 BT0: S7 WMT func on status 0'),
    ('S8 6/6 HCI probe', r'NORD2 BT0: S8 6 of 6 HCI commands'),
    ('S9 per-phone address in the controller', r'NORD2 BT0: \d+ \d+ S9 controller reports ([0-9a-f:]{17}): the per-phone address'),
    ('S10a registry', r'NORD2 BT0: \d+ \d+ S10a registry'),
    ('S10c Skywalk', r'NORD2 BT0: \d+ \d+ S10c'),
    ('S10e bridge waiting for bluetoothd', r'NORD2 BT0: \d+ \d+ S10e'),
    ('S10f bluetoothd connected', r'NORD2 BT0: \d+ \d+ S10f control connected by ([a-z]+)'),
    ('S10g channel UUID given', r'NORD2 BT0: \d+ \d+ S10g getsockopt 0x40105412: channel UUID given'),
    ('S10h Skywalk channel connected', r'NORD2 BT0: \d+ \d+ S10h'),
    ('S10i first bluetoothd command', r'NORD2 BT0: \d+ \d+ S10i bluetoothd command (0x[0-9a-f]+)'),
    ('S10g termios answered (bt5)', r'NORD2 BT0: \d+ \d+ S10g getsockopt 0x40487413 room 72 -> 0, 72 bytes'),
    ('bluetoothd HCI init passed 0x0c5b (bt5)', r'NORD2 BT1: out \d+: 01 5b 0c'),
    ('S10k address kept after HCI_Reset', r'NORD2 BT0: \d+ \d+ S10k address set again after HCI_Reset: status (0x0|0)\b'),
    ('Write Local Name answered locally (bt6)', r'NORD2 BT0: \d+ \d+ S10l (Write Local Name)'),
    ('/chosen mac-address-bluetooth0 (bt6)', r'NORD2 BT2: IODeviceTree:/chosen mac-address-bluetooth0 = ([0-9a-f:]{17})'),
    ('gestalt bluetooth true (bt6)', r'NORD2 BT U: gestalt: (bluetooth is true)'),
    ('LE Set Scan Parameters handled (bt7)', r'NORD2 BT0: \d+ \d+ S10m (LE Set Scan Parameters[^\n]*)'),
    ('0x204a kept in the kernel (bt8)', r'NORD2 BT0: \d+ \d+ S10n (LE Read Periodic Advertiser List Size)'),
    ('ACL after a connection (bt8)', r'NORD2 BT0: \d+ \d+ S11a (first ACL from the controller[^\n]*)'),
    ('pairing: link key notification (bt8)', r'NORD2 BT0: \d+ \d+ S11e (link key notification[^\n]*)'),
    ('MD coex CCIF on (bt9)', r'NORD2 BT0: \d+ \d+ S5c (MD coex CCIF[^\n]*)'),
    ('calibration saved (bt9)', r'NORD2 BT0: \d+ \d+ S7c (calibration data saved[^\n]*)'),
    ('wake vote held (bt9)', r'NORD2 BT0: \d+ \d+ S10 (BT wake vote held[^\n]*)'),
    ('registry service "bluetooth"', r'NORD2 BT2: service "bluetooth" in the registry'),
    ('kernel control registered', r'NORD2 BT1: kernel control com.apple.uart.sk.bluetooth registered'),
    ('bridge runs', r'NORD2 BT1: bridge runs'),
    ('BlueTool boot answered', r'NORD2 BT U: BlueTool RunBuiltinScript'),
    ('bluetoothd connected (kernel control)', r'NORD2 BT1: control connected by bluetoothd'),
    ('bluetoothd got the channel UUID', r'NORD2 BT1: control getsockopt 0x40105412 .*channel UUID'),
    ('Skywalk channel connected', r'NORD2 BT1: channel connected'),
]

def main():
    text = open(sys.argv[1], errors='replace').read()
    first_fail = None
    for name, rx in STAGES:
        m = re.search(rx, text)
        print('%-40s %s' % (name, ('PASS ' + (m.group(1) if m.groups() else '')) if m else 'not reached'))
    for rx in (r'NORD2 BT0: (\d+ \d+ )?FAILED at [^\n]*', r'NORD2 BT0: \d+ \d+ S0 the previous boot reset[^\n]*', r'NORD2 BT U: GUARD:[^\n]*', r'NORD2 BT0: \d+ \d+ map of[^\n]*FAILED', r'NORD2 BT0: \d+ \d+ S10x bluetoothd disconnected[^\n]*', r'NORD2 BT0: \d+ \d+ S10w no answer[^\n]*', r'NORD2 BT0: \d+ \d+ S10m [^\n]*', r'NORD2 BT0: \d+ \d+ S10[qr] [^\n]*', r'NORD2 BT0: \d+ \d+ S11[vwqrh] [^\n]*', r'NORD2 BT0: \d+ \d+ S11d [^\n]*', r'NORD2 BT0: \d+ \d+ S12m [^\n]*', r'NORD2 BT0: \d+ \d+ S6b [^\n]*', r'NORD2 BT0: S0e [^\n]*', r'NORD2 BT0: \d+ \d+ S1[01][pzb] [^\n]*', r'NORD2 BT0: \d+ \d+ S7c [^\n]*', r'NORD2 BT0: \d+ \d+ S11x [^\n]*', r'NORD2 BT0: \d+ \d+ S11e (HARDWARE ERROR|DATA BUFFER|VENDOR|disconn)[^\n]*', r'NORD2 BT0: \d+ \d+ S11s [^\n]*', r'NORD2 BT0: \d+ \d+ S10y [^\n]*', r'NORD2 BT0: \d+ \d+ S10z [^\n]*', r'NORD2 BT U: btd: [^\n]*(fail|restart)[^\n]*', r'NORD2 BT U: gestalt: [^\n]*', r'NORD2 BT0: \d+ \d+ S1 conninfra not \(yet\) on[^\n]*', r'NORD2 BT0: S5 MCU ROM never idle[^\n]*',
               r'NORD2 BT0: kern.n2bt_up refused[^\n]*', r'NORD2 BT U: (firmware missing|kern.n2bt_up refused)[^\n]*',
               r'NORD2 BT1: (receive|send of)[^\n]*failed[^\n]*', r'NORD2 BT0: BTIF TX timeout[^\n]*', r'NORD2 BT0: A-die top clock was off[^\n]*'):
        for m in re.finditer(rx, text):
            print('NOTE', m.group(0)[:200])
            first_fail = first_fail or m.group(0)
    vsc = len(re.findall(r'NORD2 BT1: out \d+: 01 [0-9a-f]{2} f[c-f]', text))
    print('vendor commands seen in the HCI trace: %d' % vsc)
    last = re.findall(r'NORD2 BT0: \d+ \d+ ([^\n]*)', text)
    print('last breadcrumb in this log:', last[-1][:160] if last else None)
    print('VERDICT', 'UP' if re.search(r'NORD2 BT1: bridge runs', text) else 'NOT UP', '|', first_fail or 'no failure line')

if __name__ == '__main__':
    main()
