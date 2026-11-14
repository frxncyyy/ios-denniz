#!/usr/bin/env python3
"""resolve_blocks.py DIFF3 OUT RULES.py: resolve the conflict blocks of a git merge-file --diff3 output with RULES.res(k, chain, base, piece)
(lists of lines -> list of lines).  Every block must be handled; each rule asserts the exact shape it expects."""
import sys, importlib.util
t = open(sys.argv[1]).read().split('\n')
spec = importlib.util.spec_from_file_location('rules', sys.argv[3]); R = importlib.util.module_from_spec(spec); spec.loader.exec_module(R)
out = []; i = 0; k = 0
while i < len(t):
    if t[i].startswith('<<<<<<< '):
        a = []; b = []; c = []; i += 1
        while not t[i].startswith('||||||| '): a.append(t[i]); i += 1
        i += 1
        while t[i] != '=======': b.append(t[i]); i += 1
        i += 1
        while not t[i].startswith('>>>>>>> '): c.append(t[i]); i += 1
        i += 1; out += R.res(k, a, b, c); k += 1
    else: out.append(t[i]); i += 1
assert k == R.N, (k, R.N)
text = '\n'.join(out)
if hasattr(R, 'post'): text = R.post(text)
open(sys.argv[2], 'w').write(text); print(sys.argv[2], k, 'blocks')
