# carve.py SRC NAME... : prints the C function definitions NAME (static ... NAME(...) { ... }) from SRC
import sys,re
src=open(sys.argv[1]).read()
for name in sys.argv[2:]:
    m=re.search(r'\n(static [^\n]*\n)?'+re.escape(name)+r'\([^)]*\)\n\{', src)
    if not m: sys.exit('missing '+name)
    i=src.index('{',m.end()-1); depth=0; j=i
    while True:
        c=src[j]
        if c=='{': depth+=1
        elif c=='}':
            depth-=1
            if depth==0: break
        j+=1
    print(src[m.start():j+1]); print()
