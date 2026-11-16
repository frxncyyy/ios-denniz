#!/usr/bin/env python3
"""Pure-python .deb reader (ar + tar with gz/xz/zst/lzma via python or the zstd CLI). Never executes package content.
  debtool.py verify-bootstrap ROOT DEBDIR LIST.json   compare every regular file of every deb with the extracted bootstrap ROOT
  debtool.py control DEB                              print control + list maintainer scripts"""
import io, json, lzma, gzip, os, subprocess, sys, tarfile, hashlib

def ar_members(data):
    assert data[:8] == b'!<arch>\n', 'not an ar archive'
    p = 8; out = {}
    while p < len(data):
        hdr = data[p:p + 60]; name = hdr[:16].decode().strip().rstrip('/'); size = int(hdr[48:58].decode().strip())
        out[name] = data[p + 60:p + 60 + size]; p += 60 + size + (size & 1)
    return out

def decomp(name, blob):
    if name.endswith('.gz'): return gzip.decompress(blob)
    if name.endswith('.xz') or name.endswith('.lzma'): return lzma.decompress(blob)
    if name.endswith('.zst'): return subprocess.run(['zstd', '-dc'], input=blob, capture_output=True, check=True).stdout
    if name.endswith('.tar'): return blob
    raise ValueError(name)

def deb_tars(path):
    m = ar_members(open(path, 'rb').read())
    ctl = [k for k in m if k.startswith('control.tar')][0]; dat = [k for k in m if k.startswith('data.tar')][0]
    return (tarfile.open(fileobj=io.BytesIO(decomp(ctl, m[ctl]))), tarfile.open(fileobj=io.BytesIO(decomp(dat, m[dat]))))

def norm(n):
    n = n[2:] if n.startswith('./') else n
    return n.rstrip('/')

def main():
    cmd = sys.argv[1]
    if cmd == 'control':
        c, d = deb_tars(sys.argv[2])
        for mem in c.getmembers():
            if mem.isfile():
                print('==', mem.name, oct(mem.mode)); print(c.extractfile(mem).read().decode(errors='replace'))
    elif cmd == 'verify-bootstrap':
        root, debdir, lst = sys.argv[2], sys.argv[3], json.load(open(sys.argv[4]))
        same = diff = missing = links_bad = 0; diffs = []
        for e in lst:
            c, d = deb_tars(os.path.join(debdir, os.path.basename(e['filename'])))
            conff = set()
            for mem in c.getmembers():
                if norm(mem.name) == 'conffiles': conff = set(l.strip().lstrip('/') for l in c.extractfile(mem).read().decode().splitlines() if l.strip())
            for mem in d.getmembers():
                n = norm(mem.name); p = os.path.join(root, n)
                if mem.isfile():
                    if not os.path.lexists(p): missing += 1; diffs.append(('missing', e['package'], n)); continue
                    a = hashlib.sha256(d.extractfile(mem).read()).hexdigest()
                    b = hashlib.sha256(open(p, 'rb').read()).hexdigest() if os.path.isfile(p) and not os.path.islink(p) else None
                    if a == b: same += 1
                    else: diff += 1; diffs.append(('differs' + (' (conffile)' if n in conff else ''), e['package'], n))
                elif mem.issym():
                    if not os.path.islink(p) or os.readlink(p) != mem.linkname: links_bad += 1; diffs.append(('symlink', e['package'], n))
        print(json.dumps({'files_identical': same, 'files_differ': diff, 'files_missing': missing, 'symlinks_bad': links_bad, 'details': diffs}, indent=1))

if __name__ == "__main__":
    main()
