#!/usr/bin/env python3
"""Per-device ADT (Apple device tree) for the Nord 2 port.

  owner, at release time:  mk_device_adt.py --make-template OWNER.adt TEMPLATE.adt
        removes the per-device properties (/chosen nord2-wifi-calibration, mac-address-wifi0) -> generic template;
        refuses if anything per-device is left.
  recipient, at install:   mk_device_adt.py TEMPLATE.adt --profile device-profile.json --wifi DUMP/wifi/WIFI OUT.adt
        adds THIS phone's Wi-Fi calibration (exact bytes of its own nvdata WIFI file, sha256 must equal the profile) and its
        factory MAC (WIFI[4:10]) -> OUT.adt (mode 0600, private).  Optional --random-seed: fresh /chosen random-seed (the
        generic template carries the fixed 64 x 0x5a of mkadt-nord2.py; untested on hardware, off by default).
"""
import argparse, hashlib, json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import n2adt

DEVICE_PROPS = ['nord2-wifi-calibration', 'mac-address-wifi0']


def make_template(src, dst):
    root = n2adt.parse_all(open(src, 'rb').read())
    ch = n2adt.child(root, 'chosen')
    removed = sum(ch.remove(k) for k in DEVICE_PROPS)
    for path, n in n2adt.walk(root):
        for k, _, _ in n.props:
            if k in DEVICE_PROPS or k.startswith('nord2-sensor') or k == 'serial-number' or k.startswith('mac-address'):
                raise SystemExit('REFUSED: per-device property %s left at %s' % (k, path))
    blob = n2adt.serialize(root)
    open(dst, 'wb').write(blob)
    print('template ADT %s: %d bytes, %d per-device properties removed, sha256 %s' % (dst, len(blob), removed, hashlib.sha256(blob).hexdigest()))


def make_device(tpl, profile, wifi, out, seed):
    prof = json.load(open(profile))
    if prof.get('verdict') not in ('READY', 'PROFILED') or 'wifi' not in prof:
        raise SystemExit('REFUSED: profile verdict %s / no valid Wi-Fi calibration' % prof.get('verdict'))
    w = open(wifi, 'rb').read()
    if hashlib.sha256(w).hexdigest() != prof['wifi']['sha256'] or w[4:10].hex() != prof['wifi']['mac']:
        raise SystemExit('REFUSED: WIFI file is not the one profiled for this phone')
    root = n2adt.parse_all(open(tpl, 'rb').read())
    ch = n2adt.child(root, 'chosen')
    if any(ch.prop(k) is not None for k in DEVICE_PROPS):
        raise SystemExit('REFUSED: the template already carries per-device properties (not a release template)')
    ch.props.append(('nord2-wifi-calibration', 0, w))
    ch.props.append(('mac-address-wifi0', 0, w[4:10]))
    if seed:
        ch.set('random-seed', os.urandom(64))
    blob = n2adt.serialize(root)
    r2 = n2adt.parse_all(blob)
    c2 = n2adt.child(r2, 'chosen')
    assert c2.prop('nord2-wifi-calibration') == w and c2.prop('mac-address-wifi0') == w[4:10]
    fd = os.open(out, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, 'wb') as f:
        f.write(blob)
    print('device ADT %s: %d bytes (Wi-Fi calibration + MAC of this phone%s)' % (out, len(blob), ', fresh random-seed' if seed else ''))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--make-template', nargs=2, metavar=('OWNER_ADT', 'TEMPLATE'))
    ap.add_argument('template', nargs='?'); ap.add_argument('out', nargs='?')
    ap.add_argument('--profile'); ap.add_argument('--wifi'); ap.add_argument('--random-seed', action='store_true')
    a = ap.parse_args()
    if a.make_template:
        make_template(*a.make_template)
    else:
        if not (a.template and a.out and a.profile and a.wifi):
            ap.error('TEMPLATE OUT --profile --wifi required')
        make_device(a.template, a.profile, a.wifi, a.out, a.random_seed)


if __name__ == '__main__':
    main()
