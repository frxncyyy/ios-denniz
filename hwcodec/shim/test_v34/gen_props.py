#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Regenerate the property table body of gpu/hwcodec/shim/n2shim_props.c from the evidence.

    python3 gen_props.py [vendor-dir] > table.inc

The key list is every property-shaped string (prefix ro. / persist. / vendor.) that the real `strings` finds in the
five vendor objects -- the same rule test_v34/run_tests.sh applies, so the table and the test cannot drift.  Values
come from the phone's own build.prop files:

    <vendor-dir>/build.prop                          (extracted /vendor)
    <repo>/gpu/build/baseband-recon-20261104/system-x/system/build.prop   (/system)
    <repo>/gpu/build/baseband-recon-20261104/vendor-x/build.prop          (real /vendor)

Every key that is not in those files is emitted as NULL = "not present on this phone; property_get returns the
caller's default".  The `why` text is derived from the key's family; the handful of keys whose reason is not
obvious are spelled out in SPECIFIC below.  The committed C file is the source of truth; this script is kept so a
new vendor object or a new build.prop can be folded in without guesswork.
"""
import os
import re
import subprocess
import sys

FILES = ['bin/vpud', 'lib/libvpud_vcodec.so', 'lib/libvcodecdrv.so', 'lib/libvcodec_utility.so', 'lib/libvcodec_oal.so']

SPECIFIC = {
    'ro.build.type': "the phone's /system/build.prop has 'user' (retail build)",
    'ro.config.low_ram': "absent from the phone's build.prop files; Android treats absent low_ram as false",
    'ro.vendor.have_aee_feature': "the phone's /system/build.prop has '1' (AEE debug feature present)",
    'ro.vendor.mtk_blowfish_tee_support': "absent from the phone's build.prop files; no Blowfish TEE path is taken",
    'ro.vendor.mtk_config_max_dram_size': "the phone's /vendor/build.prop has 0x800000000 (8 GiB DRAM window)",
    'ro.vendor.mtk_microtrust_tee_support': "absent from the phone's build.prop files; no MicroTrust TEE path is taken",
    'ro.vendor.mtk_sec_video_path_support': "the phone's /vendor/build.prop has '1'; a secure-path request still fails in the ion shim (no secure world)",
    'ro.vendor.mtk_tee_gp_support': "the phone's /vendor/build.prop has '1'",
    'ro.vendor.mtk_trustonic_tee_support': "the phone's /vendor/build.prop has '1'",
    'ro.vendor.smvr.hevc.skipnonref': "absent from the phone's build.prop files; the HEVC encoder uses its compiled default",
    'persist.vendor.radio.bitrate_ratio': 'persistent bitrate-ratio override shared with the modem stack; unset on stock, so the code default applies',
    'persist.vendor.vt.bitrate_ratio': 'persistent vendor bitrate-ratio override; unset on stock, so the code default applies',
    'persist.vendor.vt.OPTest_vcodec': 'persistent vendor VT test switch; unset on stock',
    'persist.vendor.vt.OPTest_venc': 'persistent vendor VT test switch; unset on stock',
    'vendor.max.refp.num': 'decoder reference-picture cap override; unset on stock (the driver keeps its computed value)',
    'vendor.gce.test': 'GCE test switch; unset on stock (0 = normal command lists)',
    'vendor.mtk.vpud.watch.dog': 'vpud watchdog switch read by bin/vpud; unset on stock (0 = watchdog off)',
    'vendor.mtk_deinterlace_support': 'not a property key: this string also matches the property pattern but is a feature name passed to eVideoSetParam',
    'vendor.wfd.iframesize.level': 'WFD I-frame size level; unset on stock',
}


def why(key):
    if key in SPECIFIC:
        return SPECIFIC[key]
    if key.startswith('ro.'):
        return "absent from the phone's build.prop files; unset"
    if key.startswith('persist.'):
        return 'persistent debug/override property; unset on stock'
    if key.startswith('vendor.mtk.vdec.'):
        return 'Vdec debug switch; unset on stock, so the decoder takes its compiled default'
    if key.startswith('vendor.mtk.venc.'):
        return 'Venc debug switch; unset on stock, so the encoder takes its compiled default'
    if key.startswith('vendor.mtk.omx'):
        return 'OMX/Venc debug switch; unset on stock'
    if key.startswith('vendor.gce'):
        return 'GCE debug switch; unset on stock'
    if key.startswith('vendor.vdec.'):
        return 'Vdec debug switch; unset on stock'
    if key.startswith('vendor.wfd.'):
        return 'WFD (cast) tuneable; unset on stock'
    if key.startswith('vendor.debug.'):
        return 'debug logging switch; unset on stock'
    return "vendor switch; unset on stock, so the caller's default applies"


def main():
    vendor = sys.argv[1] if len(sys.argv) > 1 else 'gpu/build/cam-vendor-20261109/vfs'
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))))  # work dir
    props = {}
    for path in (os.path.join(vendor, 'build.prop'),
                 os.path.join(root, 'gpu/build/baseband-recon-20261104/system-x/system/build.prop'),
                 os.path.join(root, 'gpu/build/baseband-recon-20261104/vendor-x/build.prop')):
        try:
            lines = open(path, errors='replace')
        except OSError:
            continue
        for line in lines:
            line = line.strip()
            if '=' in line and not line.startswith('#'):
                k, v = line.split('=', 1)
                props.setdefault(k.strip(), v.strip())
    keys = set()
    for rel in FILES:
        out = subprocess.run(['strings', '-a', os.path.join(vendor, rel)], capture_output=True, text=True, check=True)
        for s in out.stdout.splitlines():
            if re.match(r'^(ro|persist|vendor)\.[A-Za-z0-9_.-]+$', s):
                keys.add(s)
    for k in sorted(keys):
        v = props.get(k)
        val = '"%s"' % v if v is not None else 'NULL'
        print('\t{ "%s", %s,\n\t  "%s" },' % (k, val, why(k).replace('"', "'")))
    print('/* %d keys */' % len(keys), file=sys.stderr)


if __name__ == '__main__':
    main()
