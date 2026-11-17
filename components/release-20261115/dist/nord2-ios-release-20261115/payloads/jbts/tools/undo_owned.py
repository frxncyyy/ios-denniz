#!/usr/bin/env python3
"""Owned list for the jbts1 UNDO catalog check, from the guest's own 'JBTS_PATH <path>' lines in vm.log, accepted only under the
places the undo may touch; anything else the catalog check sees changed still fails the transaction.  usage: undo_owned.py VM_LOG"""
import json, re, sys
ALLOWED = (re.compile(r'^/private/var/jb(/.*)?$'), re.compile(r'^/private/var/jb\.nord2tmp$'),
           re.compile(r'^/private/var/containers/Bundle/Application/[0-9A-F-]{36}(/.*)?$'),
           re.compile(r'^/private/var/mobile/Containers/Data/InternalDaemon/[0-9A-F-]{36}/Library/Caches/[^/]*LaunchServices[^/]*\.csstore(\.nord2-prev)?$'),
           re.compile(r'^/private/var/mobile/Library/Preferences/com\.apple\.springboard\.plist$'),
           re.compile(r'^/private/var/db/nord2-jbts\.json$'))
out = {'/System/Library/xpc/launchd.plist', '/private/var/db/nord2-metal/receipt.json', '/private/var/db/nord2-jbts.json'}
for line in open(sys.argv[1], errors='replace'):
    line = line.rstrip('\r\n')
    if not line.startswith('JBTS_PATH '): continue
    p = line[len('JBTS_PATH '):]
    if not any(r.match(p) for r in ALLOWED):
        sys.exit('undo_owned: path outside the allowed places: ' + p)
    out.add(p)
print(json.dumps(sorted(out), indent=0))
