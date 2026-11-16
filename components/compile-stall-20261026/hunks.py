#!/usr/bin/env python3
"""Write a patch of the chosen hunks of `git diff -- FILE`. Usage: hunks.py FILE OUT.patch SELECTOR
SELECTOR: comma list of substrings; a hunk is kept when its text contains any of them (or 'ALL')."""
import subprocess,sys
f,out,sel=sys.argv[1],sys.argv[2],sys.argv[3]
d=subprocess.check_output(['git','diff','--',f],text=True)
head,*rest=d.split('\n@@')
hunks=['@@'+h for h in rest]
keys=sel.split(',')
kept=[h for h in hunks if sel=='ALL' or any(k in h for k in keys)]
txt=head+'\n'+'\n'.join(kept)
if not txt.endswith('\n'):txt+='\n'
open(out,'w').write(txt)
print(f,len(kept),'of',len(hunks))
