#!/usr/bin/env python3
# skel.py DIS FUNC [full]  -- print calls/strings skeleton of a function from an ipsw-style disassembly
import sys,re
dis,fn=sys.argv[1],sys.argv[2]
full=len(sys.argv)>3
on=False
for l in open(dis,errors='replace'):
    l=l.rstrip('\n')
    if l and not l.startswith('0x') and not l.startswith(' ') and l.endswith(':'):
        on = (l[:-1]==fn)
        if on: print(l)
        continue
    if not on: continue
    if full: print(l); continue
    m=re.match(r'(0x[0-9a-f]+):\s+(?:[0-9a-f]{2} ){4}\s+(.*)',l)
    if not m: continue
    a,ins=m.groups()
    if re.match(r'bl\b|blr|b\t_|ret|braa|blraa',ins) or ';' in ins and ('"' in ins or '_' in ins.split(';',1)[1]) :
        if 'stack_chk' in ins or 'os_log_type_enabled' in ins or 'logHandle' in ins or 'COREBRIGHTNESS_LOG' in ins or 'init_default_corebrightness_log' in ins: continue
        print(a,ins)
