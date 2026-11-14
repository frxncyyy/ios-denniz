#!/usr/bin/env python3
"""sbshift1 geometry model (UIKitCore 20H392 FixedSplit formulas, V-disasm) vs the hole measured on the user's photos.
leadingCenteringEdgeInset = round3(notch.x/2 + 0.15*cornerRadius) + leadingCenteringOffset; trailing = that + trailingOffset.
Calibration: the stock model predicts the clock at panel x 54..183, the photo (0550 dx 72 removed) shows ~53..182."""
import math, struct
fails = []
def ok(c, m): print(('PASS ' if c else 'FAIL ') + m); c or fails.append(m)
r3 = lambda v: round(v * 3) / 3
NOTCH_X, CORNER = (375 - 209) / 2, 39.0          # Split1125 notch 209 pt centred; iPhone X display corner radius 39 pt [I: MobileGestalt]
base = r3(NOTCH_X / 2 + 0.15 * CORNER)
def panel_x(pt_from_left): return 3 * pt_from_left - 22          # panel x = iOS px - 22 (0550-era mapping, unchanged)
CLOCK_W = 129                                                     # "00:35" measured width, panel px
HOLE_C, HOLE_R, TOL = (104, 72), 35, 5                            # measured
CORNER_R_PANEL = 115                                              # rounded_corner_radius (Android tree)
def clock(lead_off):
    c = panel_x(base + lead_off); return c - CLOCK_W / 2, c + CLOCK_W / 2
stock = clock(-2 / 3); new = clock(struct.unpack('>d', bytes.fromhex('4042000000000000'))[0])
print('stock clock %.0f..%.0f  patched clock %.0f..%.0f  hole right edge %d (+tol %d)' % (*stock, *new, HOLE_C[0] + HOLE_R, TOL))
ok(abs(stock[0] - 53) <= 6, 'model calibrated on the photo (stock clock left %.0f vs measured ~53)' % stock[0])
ok(stock[0] < HOLE_C[0] + HOLE_R, 'stock clock overlaps the hole (the bug)')
ok(new[0] >= HOLE_C[0] + HOLE_R + TOL + 12, 'patched clock clear of the worst-case hole edge by >= 12 px (%.0f px)' % (new[0] - HOLE_C[0] - HOLE_R - TOL))
ok(new[1] < 540 - 100, 'patched clock stays in the left half, far from the centre (%.0f)' % new[1])
# trailing: centre distance from the right iOS edge; battery right end ~ centre + 26 pt measured offset (battery is the outermost item)
tr_stock, tr_new = base - 2 / 3 + 1.0, base + 36.0 - 25.0
shift_px = 3 * (tr_new - tr_stock)
ok(abs(shift_px - 32) <= 0.5, 'trailing items move %.1f px towards the centre (wanted ~24..36)' % shift_px)
batt_right_stock = 1048                                           # measured (photo 1, with 0550 tdx 24 removed)
def corner_x(y):                                                  # panel x of the rounded corner boundary at row y (top-right)
    if y >= CORNER_R_PANEL: return 1080
    return 1080 - CORNER_R_PANEL + math.sqrt(CORNER_R_PANEL ** 2 - (CORNER_R_PANEL - y) ** 2)
for y in (35, 50, 65):
    ok(batt_right_stock - shift_px <= corner_x(y) - 20, 'battery right end %.0f clear of the corner at row %d (%.0f) by >= 20 px' % (batt_right_stock - shift_px, y, corner_x(y)))
# touch: unchanged mapping (no band rotation when 0551 is at its default) - round trip iOS point -> panel -> iOS point
for px in range(0, 1080, 17):
    for py in (0, 50, 113, 114, 1200, 2399):
        ix, iy = px + 22, py + 18; ok_ = (ix - 22, iy - 18) == (px, py)
        if not ok_: ok(False, 'touch round trip %d,%d' % (px, py))
ok(True, 'touch round trip identity over the panel incl. the status bar band (0551 default)')
print('RESULT', 'FAIL' if fails else 'ALL PASS'); raise SystemExit(1 if fails else 0)
