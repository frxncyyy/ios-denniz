import sys,re
sys.path.insert(0,'.')
import d
a=int(sys.argv[1],16)
t=d.fn(a)
for l in t.splitlines():
    if re.search(r'; (=|msg|@|_[A-Za-z\"])|cmp|b\.|cbz|cbnz|tbn?z|ret',l) and 'retain' not in l and 'release' not in l and 'autorelease' not in l: print(l[:160])
