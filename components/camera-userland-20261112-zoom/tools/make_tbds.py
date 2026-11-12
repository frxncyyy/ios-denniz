#!/usr/bin/env python3
"""
Writes the hand-made TBD stubs (sdk/lib/*.tbd) the bundle links against: one per install name, listing exactly the symbols the bundle's
objects import (symbol_owners.tsv). They are text files of names, not Apple's: no binary or header of the SDK is involved, and the bundle
records only the install names (/usr/lib/libSystem.B.dylib, CoreFoundation, CoreMedia, CoreVideo, /usr/lib/libobjc.A.dylib).
usage: make_tbds.py <symbol_owners.tsv> <out dir>
"""
import sys, os
LIBSYSTEM = '/usr/lib/libSystem.B.dylib'
# libsystem_platform's string/memory functions are not in the cache's patch tables (nothing patches them) but libSystem re-exports them
PLATFORM = {'_memcmp', '_memcpy', '_memset', '_strcmp', '_strlcpy', '_strlen', '_memmove'}
INFO = {
    '/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation': ('CoreFoundation', '2048.1.101', '150'),
    '/System/Library/Frameworks/CoreMedia.framework/CoreMedia': ('CoreMedia', '1.0.0', '1'),
    '/System/Library/Frameworks/CoreVideo.framework/CoreVideo': ('CoreVideo', '1.0.0', '1'),
    '/usr/lib/libobjc.A.dylib': ('libobjc.A', '228', '1'),
    LIBSYSTEM: ('libSystem', '1336', '1'),
}
groups = {}
for line in open(sys.argv[1]):
    sym, owner, status = line.rstrip('\n').split('\t')
    if status != 'cache-export':
        if sym in PLATFORM:
            owner = LIBSYSTEM
        else:
            raise SystemExit('%s: no owner' % sym)
    elif owner.startswith('/usr/lib/system/') or owner in ('/usr/lib/libSystem.B.dylib',):
        owner = LIBSYSTEM
    if owner not in INFO:
        raise SystemExit('%s: owner %s is not one of the stubs' % (sym, owner))
    groups.setdefault(owner, set()).add(sym)
groups.setdefault(LIBSYSTEM, set()).add('dyld_stub_binder')       # the linker's lazy-binding helper
os.makedirs(sys.argv[2], exist_ok=True)
for owner, syms in groups.items():
    name, cur, compat = INFO[owner]
    syms = sorted(syms)
    lines = []
    row = '                   '
    for s in syms:
        if len(row) + len(s) > 150:
            lines.append(row.rstrip())
            row = '                   '
        row += s + ', '
    lines.append(row.rstrip().rstrip(','))
    text = ("--- !tapi-tbd\ntbd-version:     4\ntargets:         [ arm64-ios ]\ninstall-name:    '%s'\ncurrent-version: %s\n"
            "compatibility-version: %s\nexports:\n  - targets:     [ arm64-ios ]\n    symbols:     [ %s ]\n...\n") % (
                owner, cur, compat, '\n'.join(lines).lstrip())
    open(os.path.join(sys.argv[2], name + '.tbd'), 'w').write(text)
    print(name, len(syms))
