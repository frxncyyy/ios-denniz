#!/bin/bash
# y2s98: rw_texlayout_pure.h on the build machine.  Nothing is installed, the phone is not touched, the mirror is only read.
#   bash rw-texlayout-host-test.sh                 the pure test (ASan/UBSan, -Werror): prints 'rw_texlayout: ok ...'
#   bash rw-texlayout-host-test.sh atx [SHEET.png] + every PosterBoard .atx of the mirror (gpu/build/mirror/rootfs-mirror.img, read with
#                                                  autocycle/hfsx.py; no write, no journal replay on the image) through
#                                                  rw_texlayout_atx_check (container as the fix expects it, layout size = chunk, decoded with
#                                                  Mesa's software ASTC decoder): prints 'rw_texlayout_atx: ok N files ...'; SHEET.png = a
#                                                  contact sheet of what the blocks decode to (needs Pillow).
# Scratch: $T if set (must exist), else mktemp -d under $TMPDIR; removed at the end unless $T was given.
set -euo pipefail
D=$(cd "$(dirname "$0")" && pwd)
W=$NORD2_WORK
B=$W/gpu/build; M=$B/y2s-stage0/mesa-25.0.7
if [ -n "${T:-}" ]; then S=$T; else S=$(mktemp -d); trap 'rm -rf "$S"' EXIT; fi
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -o "$S/t" "$D/rw_texlayout_host_test.c"
"$S/t"
[ "${1:-}" = atx ] || exit 0

# Mesa's decoder (src/mesa/main/texcompress_astc.cpp, unchanged) with the three headers it includes replaced by the little it uses
mkdir -p "$S/astc/util" "$S/files"
cp "$M/src/mesa/main/texcompress_astc.cpp" "$S/astc/"
cat > "$S/astc/texcompress_astc.h" <<'H'
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
typedef int mesa_format;                 /* here: (block width << 8) | block height, linear (not sRGB) */
#ifdef __cplusplus
extern "C" {
#endif
void _mesa_unpack_astc_2d_ldr(uint8_t *dst_row, unsigned dst_stride, const uint8_t *src_row, unsigned src_stride,
    unsigned src_width, unsigned src_height, mesa_format format);
#ifdef __cplusplus
}
#endif
static inline bool _mesa_is_format_astc_2d(mesa_format f) { return f > 0; }
static inline bool _mesa_is_format_srgb(mesa_format f) { (void)f; return false; }
static inline void _mesa_get_format_block_size(mesa_format f, unsigned *bw, unsigned *bh) { *bw = (f >> 8) & 0xff; *bh = f & 0xff; }
H
cat > "$S/astc/macros.h" <<'H'
#define MIN2(a, b) ((a) < (b) ? (a) : (b))
#define MAX2(a, b) ((a) > (b) ? (a) : (b))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define unreachable(s) __builtin_unreachable()
H
cat > "$S/astc/util/half_float.h" <<'H'
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#define FP16_ONE  0x3C00
#define FP16_ZERO 0
static inline uint16_t _mesa_uint16_div_64k_to_half(uint16_t v) { (void)v; abort(); }   /* the HDR output path: not taken (unorm8) */
H
c++ -std=c++17 -O2 -w -I"$S/astc" -c "$S/astc/texcompress_astc.cpp" -o "$S/astc.o"
cc -std=c11 -O1 -g -Wall -Wextra -Werror -I"$D" -c "$D/rw_texlayout_atx_check.c" -o "$S/check.o"
c++ -o "$S/atx_check" "$S/check.o" "$S/astc.o"

# every .atx under PosterBoard's data store in the mirror
python3 -I - "$B" "$S/files" <<'PY'
import os, struct, sys
B, out = sys.argv[1:]
sys.path.insert(0, B + '/autocycle')
import hfsx
img = hfsx.Img(B + '/mirror/rootfs-mirror.img'); img.replay(); fs = hfsx.HFS(img)
ent = {}
i = fs.cat.first
while i:
    flink, kind, _, recs = fs.cat.node(i)
    for r in recs:
        p, nm, d = fs.cat.catkey(r)
        rt = struct.unpack('>h', d[:2])[0]
        if rt in (1, 2): ent[struct.unpack('>I', d[8:12])[0]] = (p, nm, rt)
    fs.cat.cache.clear(); i = flink
def path(cn):
    o = []
    while cn in ent and cn != 2:
        p, nm, _ = ent[cn]; o.append(nm); cn = p
    return '/' + '/'.join(reversed(o))
n = 0
for cn, (p, nm, rt) in sorted(ent.items()):
    if rt == 2 and nm.endswith('.atx') and not nm.startswith('.'):
        pa = path(cn)
        if '/PRBPosterExtensionDataStore/' not in pa: continue
        c, rec = fs.lookup(pa)
        data = fs.read_fork(fs.fork(rec[88:168]), c)
        parts = pa.split('/')
        ext = parts[parts.index('Extensions') + 1].split('.')[-1]
        open('%s/%03d-%s-%s' % (out, n, ext, nm.split('-PROPERTIES')[0][:40]), 'wb').write(data); n += 1
print('mirror: %d .atx files of PosterBoard' % n)
PY
ok=0; bad=0; : > "$S/atx.log"
for f in "$S"/files/*; do
  if "$S/atx_check" "$f" "$f.rgba" >> "$S/atx.log"; then ok=$((ok+1)); else bad=$((bad+1)); fi
done
sed "s|$S/files/||" "$S/atx.log" > "$S/atx.txt"
awk -v n="${ATX_LINES:-12}" 'NR <= n {print "  " substr($0, 1, 250)}' "$S/atx.txt"
[ "$bad" = 0 ] || { echo "rw_texlayout_atx: FAILED ($bad of $((ok+bad)) files are not what the fix expects; $S/atx.txt)"; exit 1; }
python3 -I - "$S/atx.txt" <<'PY'
import re, sys
rows = [l for l in open(sys.argv[1]) if l.startswith('ATX-OK')]
clear = [float(re.search(r'clear ([0-9.]+)%', l).group(1)) for l in rows]
opaque = [float(re.search(r'opaque ([0-9.]+)%', l).group(1)) for l in rows]
err = [float(re.search(r'error-colour ([0-9.]+)%', l).group(1)) for l in rows]
sizes = sorted(set(re.search(r' (\d+x\d+) gl', l).group(1) for l in rows))
full = sum(1 for o in opaque if o == 100.0)
assert max(err) == 0.0, 'the decoder met an invalid block'
print('rw_texlayout_atx: ok %d files (sizes %s): all pre-twiddled, watermark 0, chunk = layout size; decoded: %d fully opaque, '
      'max clear %.1f%%, no invalid block' % (len(rows), ' '.join(sizes), full, max(clear)))
PY
if [ -n "${2:-}" ]; then
python3 -I - "$S/files" "$S/atx.txt" "$2" <<'PY'
import os, re, sys
from PIL import Image
files, log, out = sys.argv[1:]
tiles = []
sizes = sorted(set((int(m.group(1)), int(m.group(2))) for m in (re.match(r'ATX-OK (\d+)x(\d+) ', l) for l in open(log)) if m))
for n in sorted(n for n in os.listdir(files) if n.endswith('.rgba')):
    raw = open(os.path.join(files, n), 'rb').read()
    for (w, h) in sizes:
        if len(raw) == w * h * 4:
            im = Image.frombytes('RGBA', (w, h), raw)
            bg = Image.new('RGBA', (w, h), (255, 0, 255, 255)); bg.alpha_composite(im)      # transparency would show magenta
            bg.thumbnail((151, 328)); tiles.append(bg.convert('RGB')); break
cols = 12; rows = (len(tiles) + cols - 1) // cols
sheet = Image.new('RGB', (cols * 155, rows * 332), (40, 40, 40))
for i, t in enumerate(tiles): sheet.paste(t, ((i % cols) * 155 + 2, (i // cols) * 332 + 2))
sheet.save(out); print('contact sheet: %s (%d pictures, transparency would be magenta)' % (out, len(tiles)))
PY
fi
