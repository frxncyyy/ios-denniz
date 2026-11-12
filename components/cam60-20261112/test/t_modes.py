#!/usr/bin/env python3
"""cam60-20261112: host check of 0340's mode table (b/bsd/dev/nord2_camdev.inc) against its register tables (b/bsd/dev/nord2_cam_tables.h):
size registers 0x034c/0x034e = w/h, line length 0x0342 / pixel clock = line_ns, the table's own frame 0x0340 and the 60/s it gives, the
DMA size within one 16 MiB buffer, the pixel rate as the vendor's, and the kernel's other users of the frame size go through the mode."""
import re, sys, os
H = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'b', 'bsd', 'dev')
tab = open(os.path.join(H, 'nord2_cam_tables.h')).read(); dev = open(os.path.join(H, 'nord2_camdev.inc')).read()
fails = 0
def ok(c, what):
    global fails
    print(('PASS  ' if c else 'FAIL  ') + what); fails += 0 if c else 1
def regs(name):
    m = re.search(r'static const uint16_t ' + name + r'\[\]\s*=\s*\{(.*?)\};', tab, re.S)
    a = [int(x, 16) for x in re.findall(r'0x[0-9a-fA-F]+', m.group(1))]
    assert len(a) % 2 == 0
    d = {}
    for r, v in zip(a[0::2], a[1::2]): d[r] = v
    return d
VENDOR = {'n2c_imx766_hsvideo': (1300800000, 8816, 1228, 2048, 1152, 517030000), 'n2c_imx766_custom2': (2284800000, 15616, 2438, 4096, 2304, 894170000)}
modes = re.findall(r'\[(\d)\] = \{ "([^"]+)", N2C_TAB\((\w+)\), (\d+), (\d+), (\d+), ([^,]+), (\d+) \}', dev)
ok(len(modes) == 2, 'two modes in n2d_imx766_modes')
for idx, name, t, w, h, line_ns, flines, rate in modes:
    w, h, line_ns, rate = int(w), int(h), int(line_ns), int(rate); flines = eval(flines)
    d = regs(t); g = lambda r: (d[r] << 8) | d[r + 1]
    pclk, ll, fl, vw, vh, vrate = VENDOR[t]
    ok(g(0x034c) == w and g(0x034e) == h, '%s: table size %dx%d = mode %dx%d' % (t, g(0x034c), g(0x034e), w, h))
    ok(g(0x0342) == ll and abs(ll * 1e9 / pclk - line_ns) < 1, '%s: line %d px at %.1f MHz = %.1f ns ~ %d' % (t, ll, pclk / 1e6, ll * 1e9 / pclk, line_ns))
    ok(g(0x0340) == fl, '%s: table frame %d lines (vendor %d)' % (t, g(0x0340), fl))
    fps = 1e9 / (flines * line_ns)
    ok(59.5 < fps < 60.5, '%s: shortest frame given out %d lines = %.2f/s' % (t, flines, fps))
    ok(flines >= g(0x0340), '%s: never shorter than the table\'s own frame' % t)
    ok(w * 10 // 8 * h + 64 <= 16 * 1024 * 1024 - 4096, '%s: DMA frame %d bytes fits a buffer' % (t, w * 10 // 8 * h))
    ok(rate == vrate, '%s: mipi pixel rate %d = vendor' % (t, rate))
ok(not re.search(r'se->w\b|se->h\b|n2d_stride\(|n2d_info\[s\]\.(line_ns|frame_lines)|info->frame_lines', dev), 'no frame size / line time taken past the mode')
ok('n2d_want_mode(in, s) != in->mode' in dev, 'a mode change ends the session (as a sensor change)')
ok(re.search(r'case REDWOOD_CAMERA_MODE: \{[^\n]*\n\s*if \(c\.a >= N2D_NMODES\) \{ return EINVAL; \}', dev) is not None, 'REDWOOD_CAMERA_MODE refuses unknown modes')
print('t_modes: %s' % ('all passed' if not fails else '%d FAILED' % fails)); sys.exit(1 if fails else 0)
