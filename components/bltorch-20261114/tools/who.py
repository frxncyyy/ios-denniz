#!/usr/bin/env python3
# who.py DIS REGEX -- list (function, line, text) for lines matching REGEX
import sys,re
rx=re.compile(sys.argv[2]); fn=None
for i,l in enumerate(open(sys.argv[1],errors='replace'),1):
    if l and l[0] not in '0 \n' and l.rstrip().endswith(':'): fn=l.strip()
    elif rx.search(l): print(i,fn,l.split('  ',2)[-1].strip()[:110])
