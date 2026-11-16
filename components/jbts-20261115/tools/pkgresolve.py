#!/usr/bin/env python3
"""Resolve wanted Procursus packages against a bootstrap's dpkg status and a (signature-verified) Packages index.
Prints JSON list [{package, version, filename, sha256, size}] of packages to add (highest version each).
Virtual 'firmware' is skipped (written at runtime by /var/jb/usr/libexec/firmware), like HakiOS mk-jb.sh does.
usage: pkgresolve.py STATUS PACKAGES WANTED..."""
import json, re, sys
from functools import cmp_to_key

def stanzas(text):
    for s in text.split('\n\n'):
        d = {}; key = None
        for line in s.split('\n'):
            if not line: continue
            if line[0] in ' \t' and key: d[key] += '\n' + line; continue
            k, _, v = line.partition(':'); key = k.strip(); d[key] = v.strip()
        if 'Package' in d: yield d

def order(c):
    if c == '~': return -1
    if c.isdigit(): return 0
    if c.isalpha(): return ord(c)
    return ord(c) + 256
def cmp_part(a, b):
    i = j = 0
    while i < len(a) or j < len(b):
        fd = 0
        while (i < len(a) and not a[i].isdigit()) or (j < len(b) and not b[j].isdigit()):
            ac = order(a[i]) if i < len(a) and not a[i].isdigit() else 0
            bc = order(b[j]) if j < len(b) and not b[j].isdigit() else 0
            if ac != bc: return ac - bc
            i += 1; j += 1
        na = re.match(r'\d*', a[i:]).group(); nb = re.match(r'\d*', b[j:]).group()
        i += len(na); j += len(nb)
        if int(na or 0) != int(nb or 0): return int(na or 0) - int(nb or 0)
    return 0
def split(v):
    e, _, r = v.rpartition(':') if ':' in v else ('0', '', v)
    u, _, d = r.rpartition('-') if '-' in r else (r, '', '0')
    return int(e or 0), u, d
def vcmp(a, b):
    ea, ua, da = split(a); eb, ub, db = split(b)
    if ea != eb: return ea - eb
    return cmp_part(ua, ub) or cmp_part(da, db)
def sat(ver, op, want):
    c = vcmp(ver, want)
    return {'>=': c >= 0, '<=': c <= 0, '>>': c > 0, '<<': c < 0, '=': c == 0, '>': c >= 0, '<': c <= 0}[op]

def parse_dep(s):
    out = []
    for group in [g.strip() for g in s.split(',') if g.strip()]:
        alts = []
        for alt in group.split('|'):
            m = re.match(r'\s*([^\s(]+)\s*(?:\(\s*([<>=]+)\s*([^)\s]+)\s*\))?', alt)
            alts.append((m.group(1), m.group(2), m.group(3)))
        out.append(alts)
    return out

def main():
    status, packages, wanted = sys.argv[1], sys.argv[2], sys.argv[3:]
    inst = {}; provided = {}
    for d in stanzas(open(status).read()):
        if 'installed' in d.get('Status', ''):
            inst[d['Package']] = d['Version']
            for p in parse_dep(d.get('Provides', '')):
                provided[p[0][0]] = p[0][2] or d['Version']
    idx = {}
    for d in stanzas(open(packages).read()):
        idx.setdefault(d['Package'], []).append(d)
        for p in parse_dep(d.get('Provides', '')):
            idx.setdefault('virtual:' + p[0][0], []).append(d)
    for k in idx: idx[k].sort(key=cmp_to_key(lambda a, b: vcmp(a['Version'], b['Version'])), reverse=True)
    chosen = {}; queue = list(wanted)
    def ok_installed(name, op, ver):
        v = chosen[name]['Version'] if name in chosen else inst.get(name) or provided.get(name)
        if v is None: return False
        return op is None or sat(v, op, ver)
    while queue:
        name = queue.pop(0)
        if name in chosen: continue
        cands = idx.get(name) or []
        assert cands, 'no package ' + name
        chosen[name] = cands[0]
        for group in parse_dep(cands[0].get('Depends', '')) + parse_dep(cands[0].get('Pre-Depends', '')):
            if any(a[0] == 'firmware' for a in group) and all(a[0] == 'firmware' or ok_installed(*a) for a in group[:1]):
                continue
            if any(ok_installed(*a) for a in group if a[0] != 'firmware'): continue
            if group[0][0] == 'firmware': continue
            pick = None
            for a in group:
                c = [d for d in idx.get(a[0], []) if a[1] is None or sat(d['Version'], a[1], a[2])]
                if c: pick = a[0]; break
            assert pick, 'unsatisfiable %r for %s' % (group, name)
            queue.append(pick)
    out = [{'package': n, 'version': d['Version'], 'filename': d['Filename'], 'sha256': d['SHA256'], 'size': int(d['Size']),
            'upgrade_of': inst.get(n)} for n, d in chosen.items()]
    print(json.dumps(out, indent=1))

if __name__ == "__main__":
    main()
