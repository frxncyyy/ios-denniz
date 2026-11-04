#!/bin/sh
# Apply the Nord 2 GPU source overlay to gpu/build/xnu-stage.  This is the
# reproducible equivalent of the manual edit used while developing; it only
# touches the stage, never the main checkout.
set -eu
cd "$(dirname "$0")/.."
STAGE=build/xnu-stage
test -d "$STAGE/xnu" || test -d "$STAGE/bsd" || {
  echo "stage missing; run tools/stage_xnu.sh first" >&2; exit 1; }
cp include/nord2_gpu_*.h "$STAGE/bsd/dev/"
cp src/nord2_gpu_*.c "$STAGE/bsd/dev/"
python3 tools/prepare_android_wa.py "${NORD2_ANDROID_WA:-../../../../../../../nord2-current-kernel/firmware/valhall-1691526.wa}" build/generated/valhall_wa_firmware.h
cp build/generated/valhall_wa_firmware.h "$STAGE/bsd/dev/valhall_wa_firmware.h"
cp src/Nord2GPU.cpp "$STAGE/iokit/Kernel/arm/Nord2GPU.cpp"
python3 - "$STAGE" <<'PY'
import sys
from pathlib import Path
stage = sys.argv[1]
touch = Path(stage) / 'bsd/dev/sweet_touch.c'
original = touch.with_suffix('.c.nord2orig')
if not original.exists():
    original.write_bytes(touch.read_bytes())
t = touch.read_text()
if 'nord2_gpu_owner_poll' not in t:
    anchor = '        nvc_poll(); /* owner-only periodic phase sampling; no exact scan-boundary claim */'
    if t.count(anchor) != 1:
        raise SystemExit('owner hook anchor mismatch')
    t = t.replace(anchor, '        extern void nord2_gpu_owner_poll(void);\n'
                         '        nord2_gpu_owner_poll(); /* serialized GPU PMIC work */\n' + anchor)
    touch.write_text(t)
p = stage + '/bsd/conf/files'
s = open(p).read()
from pathlib import Path
for source in sorted(Path('src').glob('nord2_gpu_*.c')):
    line = 'bsd/dev/' + source.name + ' standard'
    if line not in s:
        s = s.replace('bsd/dev/audio_hw.c standard',
                      'bsd/dev/audio_hw.c standard\n' + line)
open(p, 'w').write(s)
p = stage + '/iokit/conf/files.arm64'
s = open(p).read()
if 'Nord2GPU.cpp' not in s:
    s = s.replace('iokit/Kernel/arm/nord2_battery_platform.cpp optional iokitcpp',
                  'iokit/Kernel/arm/nord2_battery_platform.cpp optional iokitcpp\n'
                  'iokit/Kernel/arm/Nord2GPU.cpp optional iokitcpp')
    open(p, 'w').write(s)
p = stage + '/iokit/KernelConfigTables.cpp'
s = open(p).read()
if 'Nord2GPU' not in s:
    s = s.replace('''    "   {"
    "     'IOClass'         = SweetHIDButtons;"''','''    "   {"
    "     'IOClass'         = Nord2GPU;"
    "     'IOProviderClass' = IOPlatformDevice;"
    "     'IONameMatch'     = ('mediatek,mali', 'arm,mali-valhall');"
    "     'IOProbeScore'    = 10000:32;"
    "   },"
    "   {"
    "     'IOClass'         = SweetHIDButtons;"''')
    open(p, 'w').write(s)
PY
echo "overlay applied to $STAGE"
