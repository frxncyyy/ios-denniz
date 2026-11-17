#!/usr/bin/env python3
"""Reproduce the audio-ID proposal only inside this private bundle."""
from pathlib import Path
import difflib
import hashlib

P = Path(__file__).resolve().parent
ROOT = P.parents[3]
CURRENT = ROOT / "research/ci-39158169212/buildtree/xnu"
AUDIO = ROOT / "research/nord2/audio/revision5-review/candidate/shim/nord2-xnu"

def pinned(path, digest):
    data = path.read_bytes()
    assert hashlib.sha256(data).hexdigest() == digest, path
    return data.decode()

def once(source, old, new):
    assert source.count(old) == 1, (old, source.count(old))
    return source.replace(old, new)

def write(rel, value):
    target = P / "proposed" / rel
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(value)

sweet0 = pinned(CURRENT / "bsd/dev/sweet_touch.c",
    "a39010f46d42133aedb61eb9eeec299dd691102d0df0980ff378fab03e02f15d")
conf0 = pinned(CURRENT / "bsd/conf/files",
    "0bdc383180cdf3175acd1ed8033c0a31faee37cfc301d626a21750a405207c01")
gen0 = pinned(ROOT / "tools/nord2-mkpatch.sh",
    "7e80c4b08ddaec52ed687f667a40f712815b20151e1231a47c0a1bbf960c2bd8")

sweet = once(sweet0, '#include "wifi_main_bind.inc"\n',
             '#include "wifi_main_bind.inc"\n#include "audio_id_bind.inc"\n')
sweet = once(sweet, 'if (!nw_restore()) { return -1; }\n\tif (!ni_saved) { return 0; }',
             'if (!nw_restore()) { return -1; }\n\tif (!nab_prepare_return()) { return -1; }\n\tif (!ni_saved) { return 0; }')
sweet = once(sweet, '\tni_wifi_power_probe();\n\tfor (unsigned i = 0; i < sizeof(ni_pins)',
             '\tni_wifi_power_probe();\n\tnab_audio_id();\n\tfor (unsigned i = 0; i < sizeof(ni_pins)')
conf = once(conf0, 'bsd/dev/sweet_touch.c\t\t\tstandard\n',
            'bsd/dev/sweet_touch.c\t\t\tstandard\n'
            'bsd/dev/audio_id.c\t\t\tstandard\n'
            'bsd/dev/audio_i2c8.c\t\t\tstandard\n'
            'bsd/dev/audio_afe.c\t\t\tstandard\n')
gen = once(gen0, 'NEW_FILES="$NEW_FILES bsd/dev/dma_pool_contract.c',
           'NEW_FILES="$NEW_FILES bsd/dev/audio_id.c bsd/dev/audio_id.h bsd/dev/audio_i2c8.c '
           'bsd/dev/audio_i2c8.h bsd/dev/audio_afe.c bsd/dev/audio_afe.h bsd/dev/audio_id_bind.inc"\n'
           'NEW_FILES="$NEW_FILES bsd/dev/dma_pool_contract.c')
gen = once(gen, 'for name in wifi_firmware wifi_firmware_probe',
           'for name in audio_id audio_i2c8 audio_afe wifi_firmware wifi_firmware_probe')
gen = once(gen, 'cp shim/nord2-xnu/ufs_two_lane_adapter.inc',
           'cp shim/nord2-xnu/audio_id_bind.inc "$BT/bsd/dev/audio_id_bind.inc"\n'
           'cp shim/nord2-xnu/ufs_two_lane_adapter.inc')

write('bsd/dev/sweet_touch.c', sweet)
write('bsd/conf/files', conf)
write('tools/nord2-mkpatch.sh', gen)
for name in ('audio_id', 'audio_i2c8', 'audio_afe'):
    write(f'bsd/dev/{name}.h', (AUDIO / f'{name}.h').read_text())
    # Match the canonical patch generator's real XNU wrapper.
    write(f'bsd/dev/{name}.c', '#include <arm64/sweet_early.h>\n#if defined(NORD2)\n'
          + (AUDIO / f'{name}.c').read_text() + '\n#endif /* NORD2 */\n')
write('bsd/dev/audio_id_bind.inc', (AUDIO / 'audio_id_bind.inc').read_text())

patch = []
for rel, old, new in (("bsd/dev/sweet_touch.c", sweet0, sweet),
                      ("bsd/conf/files", conf0, conf),
                      ("tools/nord2-mkpatch.sh", gen0, gen)):
    patch += list(difflib.unified_diff(old.splitlines(True), new.splitlines(True),
                                      fromfile='a/'+rel, tofile='b/'+rel))
(P / 'integration-current.patch').write_text(''.join(patch))
print('private audio-ID proposal generated; active files unchanged')
