#!/usr/bin/env python3
"""Owned list for the jbts1 UNDO / ipainbox1 catalog check, from the guest's own 'JBTS_PATH <path>' lines in vm.log, accepted only under the
places the undo may touch; anything else the catalog check sees changed still fails the transaction.  usage: undo_owned.py VM_LOG [--apps Terraria.app,SubwaySurf.app]"""
import json, re, sys
ALLOWED = (re.compile(r'^/private/var/jb(/.*)?$'), re.compile(r'^/private/var/jb\.nord2tmp$'),
           re.compile(r'^/private/var/containers/Bundle/Application/[0-9A-F-]{36}(/.*)?$'),
           re.compile(r'^/private/var/mobile/Containers/Data/InternalDaemon/[0-9A-F-]{36}/Library/Caches/[^/]*LaunchServices[^/]*\.csstore(\.nord2-prev)?$'),
           re.compile(r'^/private/var/mobile/Library/Preferences/com\.apple\.springboard\.plist$'),
           re.compile(r'^/private/var/db/nord2-jbts\.json$'), re.compile(r'^/private/var/db/nord2-ipainbox\.json$'),
           re.compile(r'^/private/var/mobile/Documents(/ipa-inbox(/[^/]+(/[^/]+)?)?)?$'),
           re.compile(r'^(/Library/Preferences|/private/var/preferences)/Logging/Subsystems/com\.apple\.(FileProvider|DocumentManager|pluginkit)\.plist$'),
           re.compile(r'^/private/var/mobile/Containers/Shared/AppGroup/[0-9A-F-]{36}/File Provider Storage$'),
           re.compile(r'^/private/var/db/nord2-fplog1\.json$'),
           re.compile(r'^/System/Library/xpc/launchd\.plist$'), re.compile(r'^/private/var/db/nord2-metal/receipt\.json$'))
apps = sys.argv[sys.argv.index('--apps') + 1].split(',') if '--apps' in sys.argv else []
for a in apps:
    assert re.match(r'^[A-Za-z0-9 ._-]+\.app$', a), a
    ALLOWED += (re.compile('^/Applications/' + re.escape(a) + '(/.*)?$'),)
out = {'/System/Library/xpc/launchd.plist', '/private/var/db/nord2-metal/receipt.json', '/private/var/db/nord2-jbts.json'}
for line in open(sys.argv[1], errors='replace'):
    line = line.rstrip('\r\n')
    if not line.startswith('JBTS_PATH '): continue
    p = line[len('JBTS_PATH '):]
    if not any(r.match(p) for r in ALLOWED):
        sys.exit('undo_owned: path outside the allowed places: ' + p)
    out.add(p)
print(json.dumps(sorted(out), indent=0))
