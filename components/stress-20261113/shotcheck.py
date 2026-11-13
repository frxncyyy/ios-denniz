#!/usr/bin/env python3
"""Photos-grid integrity check of 1/2-scale panel screenshots (540x1200), pure PIL (no numpy).

  shotcheck.py IMG [--baseline BASE.png] [--json]       one picture (for a human)
  (used by stress_verdict.py: analyse(), pair_verdict())

Geometry: the thumbnail grid is found from the 1-pt gap lines (columns/rows that are almost all background, dark or light mode)
inside the grid band (y 70..1000 at 1/2 scale: below the status bar/title, above the segmented control and the tab bar). The
baseline's geometry is reused for later pictures of the same checkpoint (stress_run.py takes them at a fixed position: Library
tab tapped twice = scrolled to the newest photos, 2 s settle).
Detectors (per cell = the inner 80 % of a grid cell):
  black   : luma mean < 10 and std < 5 while most 4-neighbours are not dark; ignored if the baseline cell is black too
  stripes : spikes of the row-to-row difference profile (rows from different scroll positions / bands): clusters of rows with
            mean |row(i) - row(i-1)| > max(28, 5 x median); defect if clusters - baseline clusters >= 3 (>= 5 without baseline)
  swap    : R/B channel swap: cells whose colour matches the baseline cell only with R and B exchanged (>= 3 cells and >= 30 % of
            the colourful comparable cells); plus the selected-tab tint: iOS blue (0,122,255) shown orange (255,122,0)
  tabbar  : sheared/garbled tab bar (y >= 1070): row-wise difference to the baseline bar shows matching rows AND badly
            mismatching rows (>= 15 % each, mean > 18) = a band of rows at the wrong offset
Pictures that do not match the baseline as a whole (mean |G - G_base| at 54x120 > 25: another screen or scroll position) are
"not comparable": only the baseline-free rules apply (black needs >= 3 cells, stripes >= 5 clusters).
Score per picture: black 3/cell, stripes 2/cell, swap 5, tabbar 4. pair_verdict(a, b): a defect counts only if the same defect is
in BOTH pictures of a checkpoint (taken 1 s apart) -> FAIL; defects in one picture only -> WARN (transient).
"""
import json, sys
from PIL import Image, ImageChops, ImageStat

def _data(im):
    f = getattr(im, 'get_flattened_data', None)
    return list(f()) if f else list(im.getdata())


CFG = dict(band=(70, 1000), tabbar_y0=1070, bg_tol=10, gap_frac=0.80, min_cell=30)


def load(path):
    im = Image.open(path).convert('RGB')
    if im.size != (540, 1200):
        im = im.resize((540, 1200), Image.BILINEAR)
    return im


def _runs(flags, min_gap=1):
    out, s = [], None
    for i, f in enumerate(flags + [False]):
        if f and s is None: s = i
        elif not f and s is not None:
            if i - s >= min_gap: out.append((s, i))
            s = None
    return out


def _bgfrac_cols(L, x0, x1, y0, y1, bg, tol):
    """fraction of pixels within tol of bg, per column of L[y0:y1, x0:x1]"""
    m = L.crop((x0, y0, x1, y1)).point(lambda v: 255 if abs(v - bg) <= tol else 0)
    return [v / 255 for v in _data(m.resize((x1 - x0, 1), Image.BOX))]


def _bgfrac_rows(L, x0, x1, y0, y1, bg, tol):
    m = L.crop((x0, y0, x1, y1)).point(lambda v: 255 if abs(v - bg) <= tol else 0)
    return [v / 255 for v in _data(m.resize((1, y1 - y0), Image.BOX))]


def _bounds(runs, lo, hi, min_cell):
    """gap runs -> cell spans between them (including the edges)"""
    edges = [lo] + [x for r in runs for x in r] + [hi]
    spans = []
    for i in range(0, len(edges) - 1, 2):
        a, b = edges[i], edges[i + 1]
        if b - a >= min_cell: spans.append((a, b))
    return spans


def detect_grid(im, cfg=CFG):
    L = im.convert('L'); y0, y1 = cfg['band']; W = im.size[0]
    best = None
    for bg in (0, 255, 28, 242):                       # dark mode black / light mode white (+ near values)
        cols = _bgfrac_cols(L, 0, W, y0, y1, bg, cfg['bg_tol'])
        cruns = _runs([c >= cfg['gap_frac'] for c in cols])
        cruns = [r for r in cruns if r[1] - r[0] <= 8]   # gap lines are thin; wide uniform areas are not gaps
        xs = _bounds(cruns, 0, W, cfg['min_cell'])
        if len(xs) < 2: continue
        ax, bx = xs[0][0], xs[0][1]
        rows = _bgfrac_rows(L, ax, bx, y0, y1, bg, cfg['bg_tol'])
        rruns = [(a + y0, b + y0) for a, b in _runs([r >= cfg['gap_frac'] for r in rows]) if b - a <= 8]
        ys = _bounds(rruns, y0, y1, cfg['min_cell'])
        if len(ys) < 2: continue
        widths = sorted(b - a for a, b in xs); med = widths[len(widths) // 2]
        xs = [s for s in xs if abs((s[1] - s[0]) - med) <= max(4, med * 0.15)]
        ys = [s for s in ys if (s[1] - s[0]) >= med * 0.5]
        cand = dict(bg=bg, xs=xs, ys=ys, cell=med)
        if best is None or len(xs) * len(ys) > len(best['xs']) * len(best['ys']): best = cand
    return best


def _inner(box, f=0.1):
    x0, y0, x1, y1 = box; dx, dy = int((x1 - x0) * f), int((y1 - y0) * f)
    return (x0 + dx, y0 + dy, x1 - dx, y1 - dy)


def _row_spikes(Lc):
    w, h = Lc.size
    if h < 8: return 0
    d = ImageChops.difference(Lc.crop((0, 1, w, h)), Lc.crop((0, 0, w, h - 1)))
    r = _data(d.resize((1, h - 1), Image.BOX))[2:-2]
    if not r: return 0
    med = sorted(r)[len(r) // 2]; thr = max(28, 5 * med)
    return len(_runs([v > thr for v in r]))


def cell_stats(im, box):
    c = im.crop(_inner(box)); L = c.convert('L')
    st = ImageStat.Stat(c); sl = ImageStat.Stat(L)
    return dict(rgb=[round(v, 1) for v in st.mean], l=round(sl.mean[0], 1), std=round(sl.stddev[0], 1), spikes=_row_spikes(L))


def _tint(im, y0):
    t = im.crop((0, y0, im.size[0], im.size[1])); blue = orange = 0
    for r, g, b in _data(t):
        if 85 <= g <= 160:
            if b > 190 and r < 80: blue += 1
            elif r > 190 and b < 80: orange += 1
    return blue, orange


def comparable(im, base):
    a = im.split()[1].resize((54, 120), Image.BOX); b = base.split()[1].resize((54, 120), Image.BOX)
    d = ImageStat.Stat(ImageChops.difference(a, b)).mean[0]
    return d <= 25, round(d, 1)


def analyse(im, base=None, geom=None, cfg=CFG):
    """-> dict(geom, cells, defects=[(kind, cell_key, detail)], score, comparable, grid_found)"""
    geom = geom or (detect_grid(base, cfg) if base is not None else None) or detect_grid(im, cfg)
    res = dict(grid_found=bool(geom), defects=[], score=0, comparable=None, diff=None, cells=0)
    comp = False
    if base is not None:
        comp, d = comparable(im, base); res['comparable'], res['diff'] = comp, d
    if not geom:
        res['defects'].append(('nogrid', '-', 'thumbnail grid not found')); return res
    res['geom'] = dict(bg=geom['bg'], cols=len(geom['xs']), rows=len(geom['ys']), cell=geom['cell'])
    S = {}; B = {}
    for i, ys in enumerate(geom['ys']):
        for j, xs in enumerate(geom['xs']):
            box = (xs[0], ys[0], xs[1], ys[1]); S[(i, j)] = cell_stats(im, box)
            if comp: B[(i, j)] = cell_stats(base, box)
    res['cells'] = len(S)
    dark = lambda s: s['l'] < 10 and s['std'] < 5
    blacks = []
    for (i, j), s in S.items():
        if not dark(s): continue
        nb = [S[k] for k in ((i - 1, j), (i + 1, j), (i, j - 1), (i, j + 1)) if k in S]
        if nb and sum(1 for n in nb if n['l'] > 25 or n['std'] > 12) * 2 > len(nb):
            if comp and dark(B[(i, j)]): continue
            blacks.append((i, j))
    if blacks and (comp or len(blacks) >= 3):
        for k in blacks: res['defects'].append(('black', '%d,%d' % k, 'l=%.0f std=%.0f' % (S[k]['l'], S[k]['std'])))
    for k, s in S.items():
        extra = s['spikes'] - (B[k]['spikes'] if comp else 0)
        if extra >= (3 if comp else 5):
            res['defects'].append(('stripes', '%d,%d' % k, 'row-jump clusters %d (baseline %s)' % (s['spikes'], B[k]['spikes'] if comp else '-')))
    if comp:
        colourful = votes = 0
        for k, s in S.items():
            b = B[k]
            if abs(b['rgb'][0] - b['rgb'][2]) <= 25 or abs(s['rgb'][1] - b['rgb'][1]) > 15: continue
            colourful += 1
            same = sum(abs(x - y) for x, y in zip(s['rgb'], b['rgb']))
            swap = abs(s['rgb'][2] - b['rgb'][0]) + abs(s['rgb'][1] - b['rgb'][1]) + abs(s['rgb'][0] - b['rgb'][2])
            if swap + 10 < same and swap < 25: votes += 1
        if votes >= 3 and votes >= 0.3 * colourful:
            res['defects'].append(('swap', 'grid', '%d of %d colourful cells match the baseline only with R/B swapped' % (votes, colourful)))
        bb, bo = _tint(base, cfg['tabbar_y0']); cb, co = _tint(im, cfg['tabbar_y0'])
        if bb > 50 and cb < 10 and co > 50:
            res['defects'].append(('swap', 'tabbar', 'selected tab tint orange (%d px) instead of blue (baseline %d px blue)' % (co, bb)))
        y0 = cfg['tabbar_y0']; W, H = im.size
        d = ImageChops.difference(im.crop((0, y0, W, H)).convert('L'), base.crop((0, y0, W, H)).convert('L'))
        rows = _data(d.resize((1, H - y0), Image.BOX)); n = len(rows)
        mean = sum(rows) / n; good = sum(1 for v in rows if v < 8) / n; bad = sum(1 for v in rows if v > 35) / n
        if mean > 18 and good >= 0.15 and bad >= 0.15:
            res['defects'].append(('tabbar', 'tabbar', 'rows matching %.0f%% / mismatching %.0f%% (mean diff %.0f)' % (good * 100, bad * 100, mean)))
    else:
        cb, co = _tint(im, cfg['tabbar_y0'])
        if co > 80 and cb < 10: res['notes'] = ['tab tint looks orange (%d px), no comparable baseline to confirm' % co]
    W8 = dict(black=3, stripes=2, swap=5, tabbar=4, nogrid=0)
    res['score'] = sum(W8[k] for k, _, _ in res['defects'])
    return res


def self_ok(res):
    """usable as a baseline: grid found, enough cells, no defects"""
    return res['grid_found'] and res['cells'] >= 6 and not [d for d in res['defects'] if d[0] != 'nogrid']


def pair_verdict(ra, rb):
    """two pictures of one checkpoint -> (verdict, persistent defects, transient defects, score)"""
    ka = {(k, c) for k, c, _ in ra['defects'] if k != 'nogrid'}; kb = {(k, c) for k, c, _ in rb['defects'] if k != 'nogrid'}
    pers = sorted(ka & kb); trans = sorted(ka ^ kb)
    W8 = dict(black=3, stripes=2, swap=5, tabbar=4)
    score = sum(W8[k] for k, _ in pers) + sum(1 for _ in trans)
    v = 'FAIL' if pers else 'WARN' if trans or not (ra['grid_found'] and rb['grid_found']) else 'PASS'
    return v, pers, trans, score


def main(argv):
    if not argv or argv[0] in ('-h', '--help'): print(__doc__); return 0
    im = load(argv[0]); base = load(argv[argv.index('--baseline') + 1]) if '--baseline' in argv else None
    r = analyse(im, base)
    if '--json' in argv: print(json.dumps(r, indent=1, default=str))
    else:
        print('grid %s cells=%d comparable=%s diff=%s score=%d' % (r.get('geom'), r['cells'], r['comparable'], r['diff'], r['score']))
        for d in r['defects']: print('  %-8s %-7s %s' % d)
    return 1 if r['score'] else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
