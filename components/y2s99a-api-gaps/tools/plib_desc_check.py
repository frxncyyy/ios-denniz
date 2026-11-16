# For each mirror .pipelinelib: library names in pipelines.desc, and how many of the desc's identifier strings name a
# function present (as a string) in that library -- a rough check that the descriptor path has AIR to compile from.
import os, re, sys
root = sys.argv[1]
for dp, dn, fn in os.walk(root):
    if not dp.endswith('.pipelinelib') or 'pipelines.desc' not in fn: continue
    desc = open(os.path.join(dp, 'pipelines.desc'), 'rb').read()
    strs = [s.decode() for s in re.findall(rb'[A-Za-z_][A-Za-z0-9_:.\-]{3,}', desc)]
    libs = sorted({s for s in strs if s.endswith('.metallib')})
    parent = os.path.dirname(dp)
    have = {}
    for l in libs:
        p = os.path.join(parent, l)
        have[l] = open(p, 'rb').read() if os.path.exists(p) else None
    funcs = [s for s in strs if not s.endswith('.metallib')]
    found = sum(1 for s in set(funcs) if any(b is not None and (s.encode() + b'\0') in b for b in have.values()))
    print('%-95s libs %s present %s | %d of %d distinct names found in the libraries' % (dp[len(root):], libs,
          [have[l] is not None for l in libs], found, len(set(funcs))))
