#!/usr/bin/env python3
# usage: n2d.py script.txt [tag]  -> runs ops on phone, prints N2D lines (also saved to out/<tag>.log)
import struct, subprocess, sys, os, time
ADB = os.path.expanduser('~/Android/Sdk/platform-tools/adb')
HERE = os.path.dirname(os.path.abspath(__file__))
def sh(c, inp=None):
    import shlex
    return subprocess.run([ADB, 'shell', 'su -c ' + shlex.quote(c)], capture_output=True, input=inp).stdout
_ks = None
def ksym(name):
    global _ks
    if _ks is None:
        _ks = {}
        for l in sh('echo 0 > /proc/sys/kernel/kptr_restrict; cat /proc/kallsyms').decode().splitlines():
            p = l.split()
            if len(p) >= 3: _ks.setdefault(p[2], int(p[0], 16))
    assert _ks[name], 'kallsyms zero'
    return _ks[name]
def val(x):
    if x.startswith('%'): return (0xee << 56) | int(x[1:])
    if x.startswith('&'):
        s, _, off = x[1:].partition('+'); return ksym(s) + (int(off, 0) if off else 0)
    return int(x, 0)
def build(lines):
    strs = b''; ops = []
    for l in lines:
        l = l.split('#')[0].strip()
        if not l: continue
        p = l.split(); c = p[0].upper(); a = [val(x) for x in p[1:]] if c not in ('T', 'STR') else [int(p[1], 0)] if c == 'STR' else []
        if c == 'R': ops.append((1, a[0], a[1] if len(a) > 1 else 1, 0))
        elif c == 'W': ops.append((2, a[0], a[1], 0))
        elif c == 'G': ops.append((3, a[0], a[1], a[2]))
        elif c == 'E': ops.append((4, 0, 0, 0))
        elif c == 'C': ops.append((5, a[0], a[1] if len(a) > 1 else 0, a[2] if len(a) > 2 else 0))
        elif c == 'K': ops.append((6, a[0], a[1], 0))
        elif c == 'P': ops.append((7, a[0], a[1], a[2] if len(a) > 2 else 0))
        elif c == 'S': ops.append((8, a[0], 0, 0))
        elif c == 'X': ops.append((9, a[0], a[1], a[2]))
        elif c == 'F': ops.append((11, a[0], a[1], a[2]))
        elif c == 'Q': ops.append((12, a[0], a[1], a[2]))
        elif c == 'SET': ops.append((14, a[0], a[1], a[2] if len(a) > 2 else 0))
        elif c == 'CALLR': ops.append((13, a[0], a[1], 0))
        elif c == 'LD': ops.append((16, a[0], a[1], a[2] if len(a) > 2 else 0))
        elif c == 'STR':
            ops.append((14, a[0], (0xef << 56) | len(strs), 0)); strs += ' '.join(p[2:]).encode() + b'\0'; continue
        elif c == 'ADD': ops.append((18, a[0], a[1], a[2]))
        elif c == 'NZ': ops.append((19, a[0], 0, 0))
        elif c == 'Z': ops.append((20, a[0], 0, 0))
        elif c == 'WATCHR': ops.append((21, a[0], a[1], a[2]))
        elif c == 'WAITCAM': ops.append((22, a[0], a[1], 0))
        elif c == 'WATCH': ops.append((17, a[0], a[1], a[2]))
        elif c == 'T':
            ops.append((10, len(strs), 0, 0)); strs += ' '.join(p[1:]).encode() + b'\0'
        else: raise SystemExit('bad op ' + l)
    assert len(ops) < 250 and len(strs) < 1024
    prot = 3 | 0x400 | 0x300 | 0x4 | (1 << 11) | (1 << 51) | (1 << 53) | (1 << 54) | (1 << 55)
    blob = b'N2DOPS01' + struct.pack('<5Q', ksym('printk'), ksym('__ioremap'), ksym('__iounmap'), ksym('msleep'), prot)
    blob += strs.ljust(1024, b'\0') + b''.join(struct.pack('<4Q', *o) for o in ops)
    return blob
def run(lines, tag='run'):
    ko = bytearray(open(os.path.join(HERE, 'n2d.ko'), 'rb').read())
    i = ko.index(b'N2DOPS01'); b = build(lines); ko[i:i + len(b)] = b
    p = os.path.join(HERE, 'n2d_run.ko'); open(p, 'wb').write(ko)
    subprocess.run([ADB, 'push', p, '/data/local/tmp/n2d.ko'], capture_output=True)
    mark = 'n2dmark%d' % time.time()
    sh('echo %s > /dev/kmsg; insmod /data/local/tmp/n2d.ko' % mark)
    out = sh('dmesg').decode(errors='replace').splitlines()
    k = max([i for i, l in enumerate(out) if mark in l] or [0])
    res = [l for l in out[k:] if 'N2D' in l or 'n2d' in l]
    os.makedirs(os.path.join(HERE, 'out'), exist_ok=True)
    open(os.path.join(HERE, 'out', tag + '.log'), 'w').write('\n'.join(res) + '\n')
    return res
if __name__ == '__main__':
    lines = open(sys.argv[1]).read().splitlines()
    for l in run(lines, sys.argv[2] if len(sys.argv) > 2 else os.path.basename(sys.argv[1])): print(l)
