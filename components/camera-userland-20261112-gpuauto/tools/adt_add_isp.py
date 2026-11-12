#!/usr/bin/env python3
"""
Adds the /arm-io/isp node that makes Camera.app's home-screen icon appear -- the clean alternative to HakiOS's SpringBoard hook. Writes a NEW ADT
file; nothing is installed or flashed.

Why: SpringBoard shows an icon tagged "still-camera" (Camera.app's SBAppTags) only if "still-camera" is among the visible icon tags, which come from
MobileGestalt's boolean answers. In libMobileGestalt of the iOS 16.7 cache the answer "still-camera" (key nv4RoLkNoPT0/rsO8Yaiew =
base64(md5("MGCopyAnswer"+"still-camera"))[:22]) is the function at 0x19c56c570: it is true when the device tree's isp node has the property
"camera-front", else it reads IODeviceTree:/arm-io/isp's property "camera-rear" (the key X9NA3D+PguwY0i0Uffl07Q, i.e. "camera-rear"). The Nord 2's
ADT has no isp node (the D22AP tree the /product node is copied from has one, with camera-front = camera-rear = 1; here only those two are kept).
usage: adt_add_isp.py <in.adt> <out.adt>
"""
import os, struct, sys
sys.path.insert(0, os.environ.get('IOS_SWEET_TOOLS', '$NORD2_REPO/tools'))
import adt

root, end = adt.parse(adt.load(sys.argv[1]))
arm_io = [c for c in root.children if c.name == 'arm-io']
if len(arm_io) != 1:
    raise SystemExit('expected exactly one /arm-io node, found %d' % len(arm_io))
arm_io = arm_io[0]
if any(c.name == 'isp' for c in arm_io.children):
    raise SystemExit('/arm-io/isp is already there')


def phandles(n):
    for k, f, v in n.props:
        if k == 'AAPL,phandle' and len(v) == 4:
            yield struct.unpack('<I', v)[0]
    for c in n.children:
        yield from phandles(c)


u32 = lambda v: struct.pack('<I', v)
cstr = lambda s: s.encode() + b'\0'
isp = adt.Node()
isp.props = [('name', 0, cstr('isp')), ('device_type', 0, cstr('isp')), ('AAPL,phandle', 0, u32(max(phandles(root)) + 1)),
             ('camera-front', 0, u32(1)), ('camera-rear', 0, u32(1))]
arm_io.children.append(isp)
open(sys.argv[2], 'wb').write(adt.serialize(root))
print('added /arm-io/isp (camera-front = camera-rear = 1) ->', sys.argv[2])
