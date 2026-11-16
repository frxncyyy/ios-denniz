#!/usr/bin/env python3
"""extract.py FILE NAME... : print the definitions (signature line(s) + body up to the first '}' in column 0) of the named functions"""
import re, sys
src = open(sys.argv[1]).read().split('\n')
for name in sys.argv[2:]:
    for i, l in enumerate(src):
        if re.match(r'^%s\(' % re.escape(name), l):
            s = i - 1 if i > 0 and src[i-1] and not src[i-1].startswith(('}', '/', ' ', '\t', '#')) else i
            j = i
            while src[j] != '}': j += 1
            print('\n'.join(src[s:j+1])); print(); break
    else:
        sys.exit('missing ' + name)
