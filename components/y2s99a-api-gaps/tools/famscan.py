# for each objc_msgSend$supportsFamily:/supportsFeatureSet: call site: the last 'mov w2/x2, #imm' within 12 lines before, and the function
import re, sys
for f in sys.argv[1:]:
    lines = open(f, errors='replace').read().splitlines()
    func = '?'
    for i, l in enumerate(lines):
        if l.endswith(':') and not l.startswith('0x'):
            func = l[:-1]
        m = re.search(r'objc_msgSend\$(supportsFamily:|supportsFeatureSet:|supportsTextureSampleCount:)', l)
        if not m: continue
        imm = '?'
        for j in range(i - 1, max(0, i - 14), -1):
            mm = re.search(r'mov\s+[wx]2, #(0x[0-9a-f]+|\d+)', lines[j])
            if mm: imm = str(int(mm.group(1), 0)); break
            if re.search(r'\bbl\s', lines[j]): break
        print('%s %s %s %s' % (f.split('/')[-1][:-4], m.group(1), imm, func[:110]))
