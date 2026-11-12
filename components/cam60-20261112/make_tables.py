#!/usr/bin/env python3
"""cam60-20261112: appends the IMX766 hs_video (2048x1152, 120/s as the vendor runs it) and custom2 (4096x2304, 60/s) register tables of the
OnePlus GPL driver that n2c_imx766_init / n2c_imx766_preview came from (mt6885/camera_project/k6893v1_64/imx766_mipi_raw21015, the same
arrays byte for byte: checked here) to nord2_cam_tables.h.   usage: make_tables.py <imx766mipiraw_Sensor.c> <nord2_cam_tables.h in/out>"""
import re, sys
src = open(sys.argv[1], errors='replace').read(); path = sys.argv[2]; ours = open(path).read()
def vend(name):
    m = re.search(r'static kal_uint16 ' + name + r'\[\]\s*=\s*\{(.*?)\};', src, re.S)
    return [int(x, 16) for x in re.findall(r'0x[0-9a-fA-F]+', re.sub(r'//[^\n]*|/\*.*?\*/', '', m.group(1), flags=re.S))]
def our(name):
    m = re.search(r'static const uint16_t ' + name + r'\[\]\s*=\s*\{(.*?)\};', ours, re.S)
    return [int(x, 16) for x in re.findall(r'0x[0-9a-fA-F]+', m.group(1))]
assert vend('imx766_init_setting') == our('n2c_imx766_init'), 'not the driver our init table came from'
assert vend('imx766_capture_30_setting') == our('n2c_imx766_preview'), 'not the driver our preview table came from'
assert 'n2c_imx766_hsvideo' not in ours
out = ['', '/* cam60-20261112 (0340): the same driver\'s hs_video (QBIN V2H2, 2048x1152, frame 1228 lines of 8816 px at 1300.8 MHz = 120/s; given',
       ' * out with frames twice as long: 60/s) and custom2 (QBIN, 4096x2304, 2438 lines of 15616 px at 2284.8 MHz = 60/s). */']
for v, o in (('imx766_hs_video_setting', 'n2c_imx766_hsvideo'), ('imx766_custom2_setting', 'n2c_imx766_custom2')):
    t = vend(v); assert len(t) % 2 == 0 and len(t) > 100
    out.append('static const uint16_t %s[] = {' % o)
    for i in range(0, len(t), 8):
        out.append('\t' + ' '.join('0x%04x,' % x for x in t[i:i + 8]))
    out.append('};')
open(path, 'w').write(ours.rstrip('\n') + '\n' + '\n'.join(out) + '\n')
print('appended', [(v, len(vend(v)) // 2) for v in ('imx766_hs_video_setting', 'imx766_custom2_setting')])
