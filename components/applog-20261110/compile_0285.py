#!/usr/bin/env python3
"""Compile bsd/kern/kern_exec.c (patch 0285, applog) for the Nord 2 relink, keeping its typed-allocator views exact.

    compile_0285.py SOURCE ORIG_OBJECT OUT_OBJECT [--name-default NAMES] [--include-dir DIR]

kern_exec.c uses kalloc_type/kfree_type, and the local clang has no __builtin_xnu_type_signature /
__builtin_xnu_type_summary (Apple clang only). The usual shim of gpu/tools/xnu_staged_compile.py
(signature "", summary 0) would mark every view KT_DATA_ONLY, and the registered-ports array that
exec_handle_port_actions allocates here is freed by mach_ports_register (osfmk/kern/ipc_tt.c, a CI
object with the real view): different heaps. So the views (name, flags, sizes, signatures) are read
from ORIG_OBJECT (the CI kern_exec.o of the startup archive), the builtins are defined per sizeof()
to reproduce them, and the result is compared view by view: any difference fails the build.
Host only; writes OUT_OBJECT and OUT_OBJECT.views.json.
"""
import argparse, json, re, struct, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]          # .../work
BIN = '$HOME/Documents/Codex/2026-10-13/por/work/huawei/command-line-tools/sdk/default/openharmony/native/llvm/bin'
KT_DATA_ONLY, KT_PTR_ARRAY = 0x08, 0x80
GR_PTR, GR_DATA = 1 << 1, 1 << 2                      # KT_GRANULE_POINTER, KT_GRANULE_DATA summary bits


def secdump(obj, name):
    out = subprocess.check_output([BIN + '/llvm-objdump', '-s', '-j', name, str(obj)], text=True)
    base, data = None, b''
    for l in out.splitlines():
        m = re.match(r'^\s*([0-9a-f]+) ((?:[0-9a-f]+ ){1,4})', l)
        if m:
            if base is None:
                base = int(m.group(1), 16)
            data += bytes.fromhex(m.group(2).replace(' ', ''))
    return base, data


def views(obj):
    """sorted [(kind, name, flags, size_hdr, size_type, sig_hdr, sig_type)] of __kalloc_type and __kalloc_var"""
    syms = {}
    for l in subprocess.check_output([BIN + '/llvm-nm', str(obj)], text=True).splitlines():
        p = l.split()
        if len(p) == 3:
            syms[p[2]] = int(p[0], 16)
    cb, cd = secdump(obj, '__cstring')
    rel, cur = {}, None
    for l in subprocess.check_output([BIN + '/llvm-objdump', '-r', str(obj)], text=True).splitlines():
        m = re.match(r'RELOCATION RECORDS FOR \[(\S+)\]', l)
        if m:
            cur = m.group(1)
            continue
        p = l.split()
        if cur and len(p) == 3 and 'UNSIGNED' in p[1]:
            rel[(cur, int(p[0], 16))] = p[2]

    def ptr(sec, off):
        r = rel.get((sec, off))
        if r is None:
            return None
        o = syms[r] - cb
        return cd[o:cd.index(b'\0', o)].decode()
    res = []
    _, d = secdump(obj, '__kalloc_type')               # struct kalloc_type_view, 64 bytes
    assert len(d) % 64 == 0
    for i in range(0, len(d), 64):
        fl, sz = struct.unpack_from('<II', d, i + 40)
        res.append(('fixed', ptr('__kalloc_type', i + 16), fl, 0, sz, None, ptr('__kalloc_type', i + 32)))
    _, d = secdump(obj, '__kalloc_var')                # struct kalloc_type_var_view, 80 bytes
    assert len(d) % 80 == 0
    for i in range(0, len(d), 80):
        ver, sh, st = struct.unpack_from('<HHI', d, i)
        fl, = struct.unpack_from('<I', d, i + 72)
        assert ver == 1
        res.append(('var', ptr('__kalloc_var', i + 16), fl, sh, st, ptr('__kalloc_var', i + 56), ptr('__kalloc_var', i + 64)))
    return sorted(res, key=repr)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('source', type=Path)
    ap.add_argument('orig', type=Path)
    ap.add_argument('out', type=Path)
    ap.add_argument('--include-dir', type=Path, action='append', default=[], help='directory of the private headers kern_exec.c includes with "" (the pristine stage bsd/kern) when SOURCE is alone in a scratch tree')
    ap.add_argument('--name-default', default='', help='NORD2_APPLOG_NAME_DEFAULT (applog_name when the boot-arg is absent)')
    a = ap.parse_args()
    assert re.fullmatch(r'[A-Za-z0-9_.,-]*', a.name_default), a.name_default
    want = views(a.orig)
    assert want, 'no kalloc_type views in the original object'
    by_size = {}
    for kind, name, fl, sh, st, sig_hdr, sig in want:
        assert sh == 0 and sig_hdr is None, 'header+array views are not handled'
        assert sig is not None and '"' not in sig and '\\' not in sig
        summary = GR_DATA if fl & KT_DATA_ONLY else GR_PTR if fl & KT_PTR_ARRAY else GR_PTR | GR_DATA
        assert by_size.setdefault(st, (sig, summary)) == (sig, summary), ('sizeof() does not identify the type', st)
    sig_expr, sum_expr = '""', str(GR_PTR | GR_DATA)
    for size, (sig, summary) in sorted(by_size.items()):
        sig_expr = '(sizeof(t)==%d?"%s":%s)' % (size, sig, sig_expr)
        sum_expr = '(sizeof(t)==%d?%d:%s)' % (size, summary, sum_expr)
    extra = ['-Wno-unsafe-buffer-usage', '-Wno-unused-but-set-variable', '-Wno-cast-align', '-Wno-sign-compare',
             '-D__builtin_xnu_type_signature(t)=' + sig_expr, '-D__builtin_xnu_type_summary(t)=' + sum_expr,
             '-D__builtin_xnu_types_compatible(...)=1', '-DNORD2_APPLOG_NAME_DEFAULT="%s"' % a.name_default] + ['-I' + str(d.resolve()) for d in a.include_dir]
    r = subprocess.run(['nice', '-n', '10', 'python3', str(ROOT / 'gpu/tools/xnu_staged_compile.py'), str(a.source), '--out', str(a.out)]
                       + ['--extra=' + x for x in extra], cwd=ROOT)
    if r.returncode:
        return r.returncode
    got = views(a.out)
    Path(str(a.out) + '.views.json').write_text(json.dumps({'original': want, 'compiled': got, 'equal': got == want}, indent=1) + '\n')
    if got != want:
        print('kalloc_type views differ from the original object', file=sys.stderr)
        for x in want:
            if x not in got:
                print('  missing', x, file=sys.stderr)
        for x in got:
            if x not in want:
                print('  extra  ', x, file=sys.stderr)
        return 1
    print('kalloc_type views identical to the original object:', len(got), file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main())
