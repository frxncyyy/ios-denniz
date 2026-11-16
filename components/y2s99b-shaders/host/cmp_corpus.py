import os, re, sys, collections
base, new = sys.argv[1], sys.argv[2]
pat = re.compile(r'^  (\S.*?)\s{2,}(NOT TRANSLATED: .*|MESA_SHADER_\w+, \d+ bytes of Valhall code \((\w+)\).*|THE COMPILER DIED.*)$')
def parse(f):
    d = {}
    for l in open(f, errors='replace'):
        m = pat.match(l.rstrip('\n'))
        if m:
            name, rest, h = m.group(1), m.group(2), m.group(3)
            st = 'ok:' + h if h else ('refused' if rest.startswith('NOT') else 'died')
            d.setdefault(name, (st, rest[:160]))
    return d
cnt = collections.Counter(); gained = []; lost = []; changed = []
for fn in sorted(os.listdir(new)):
    fb, fnw = os.path.join(base, fn), os.path.join(new, fn)
    if not os.path.exists(fb) or 'exit' not in open(fnw, errors='replace').read()[-20:] : cnt['skipped-lib'] += 1; continue
    b, n = parse(fb), parse(fnw)
    for k in set(b) | set(n):
        sb, sn = b.get(k, ('absent', ''))[0], n.get(k, ('absent', ''))[0]
        if sb == sn: cnt['same'] += 1
        elif sb.startswith('ok') and sn.startswith('ok'): changed.append((fn[:12], k)); cnt['ok-hash-changed'] += 1
        elif sn.startswith('ok'): gained.append((fn[:12], k, b.get(k, ('', ''))[1][:90])); cnt['gained'] += 1
        elif sb.startswith('ok'): lost.append((fn[:12], k, n.get(k, ('', ''))[1])); cnt['LOST'] += 1
        else: cnt['still-not'] += 1
print(dict(cnt))
print('LOST:'); [print(' ', *x) for x in lost]
print('hash changed:'); [print(' ', *x) for x in changed[:40]]
print('gained (first 60):'); [print(' ', *x) for x in gained[:60]]
