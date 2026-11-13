#!/usr/bin/env python3
"""Wi-Fi scan-path guard (q10jgw regression). usage: check_scan_path.py CANDIDATE_DIR REFERENCE_DIR
A. wifi_normal_rx.o: the prebuilt CI functions are in the linked kernel, once.
B. wifi_normal_transport.o: the same for the CI functions; with 0436 (CANDIDATE/k/kernel/binpatch-0436 exists) the expected object is the
   CI member + wifi_binpatch.patch(): the archive member must be byte-equal to it (= CI apart from the 16 intended branch words), the four
   original shape clusters must be absent from the kernel and the four NOP-ed ones present once each.
C. sweet_touch.o (the binders): without 0436 its __text must equal the reference's. With 0436 the binder is rebuilt on purpose: every function
   is compared with the reference after masking addresses/branch targets, and only the functions named in ALLOWED may differ or be new
   (the scan owner loop with nss_execute inlined, the address-change scan, the station service entry points, the worker that holds nss_start)."""
import sys,subprocess,re,struct,hashlib,glob,tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent)); import wifi_binpatch
LL='$HOME/Documents/Codex/2026-10-13/por/work/huawei/command-line-tools/sdk/default/openharmony/native/llvm/bin/'
ALLOWED={'_nss_owner_loop','_nss_execute_mac','_nss_build_mac','_nss_diag','_nss_diag_state','_ncd_worker','_nord2_wifi_join_enqueue',
         '_njs_mac_scan_done','_njs_accept_locked','_njs_initialize_locked'}
import os
ALLOWED|=set(os.environ.get('SCANPATH_EXTRA_ALLOWED','').split())   # explicit per-run extras (e.g. 0496 torch in the input thread); empty = unchanged behaviour
GROWTH={112,16}   # nss_native / njs_native grew at their END: only __memset_chk/__memcpy_chk object-size immediates move by this much
def bound_only(x,y):
    if x is None or y is None or len(x)!=len(y): return False
    for u,v in zip(x,y):
        if u==v: continue
        m,n=re.fullmatch(r'mov\tw(\d+), #(\d+)',u),re.fullmatch(r'mov\tw(\d+), #(\d+)',v)
        if not m or not n or m.group(1)!=n.group(1) or int(m.group(2))-int(n.group(2)) not in GROWTH: return False
    return True
C,Rf=Path(sys.argv[1]),Path(sys.argv[2]); B=Path(__file__).resolve().parents[1]; ok=True
def text(o):
    t=subprocess.check_output([LL+'llvm-objdump','-s','--section=__text',str(o)],text=True)
    return hashlib.sha256('\n'.join(l.split('  ')[0] for l in t.split('\n') if re.match(r'^\s*[0-9a-f]+ ',l)).encode()).hexdigest()
def runs(obj,sym):
    t=subprocess.check_output([LL+'llvm-objdump','-d','-r','--disassemble-symbols='+sym,str(obj)],text=True);words=[];run=[]
    for l in t.split('\n'):
        m=re.match(r'^\s*[0-9a-f]+:\s+([0-9a-f]{8})\s',l)
        if m: run.append(struct.pack('<I',int(m.group(1),16)))
        elif 'RELOC' in l:
            if len(run)>1: words.append(b''.join(run[:-1]))
            run=[]
    if run: words.append(b''.join(run))
    return max(words,key=len)
def funcs(obj):
    t=subprocess.check_output([LL+'llvm-objdump','-d','--no-show-raw-insn',str(obj)],text=True);cur=None;f={}
    for l in t.split('\n'):
        m=re.match(r'^[0-9a-f]+ <(.+)>:$',l)
        if m: cur=m.group(1);f[cur]=[];continue
        m=re.match(r'^\s*[0-9a-f]+:\s+(.*)$',l)
        if m and cur: f[cur].append(re.sub(r'0x[0-9a-f]+ <[^>]*>','@',m.group(1)).split(';')[0].strip())
    return f
kern=open(glob.glob(str(C/'k/kernel/kernel.development.nord2-metal-*'))[0],'rb').read()
patched=(C/'k/kernel/binpatch-0436').is_dir()
with tempfile.TemporaryDirectory(dir=str(C)) as d:
    subprocess.run([LL+'llvm-ar','x',str(B/'startup-diagnostic/kernel.startup.a'),'wifi_normal_transport.o','wifi_normal_rx.o'],cwd=d,check=True)
    ci=open(d+'/wifi_normal_transport.o','rb').read(); exp=ci
    if patched:
        exp,sites=wifi_binpatch.patch(ci); open(d+'/wifi_normal_transport.o','wb').write(exp)
        got=(C/'k/kernel/binpatch-0436/wifi_normal_transport.o').read_bytes()
        diff=[i for i in range(len(ci)) if got[i]!=ci[i]] if len(got)==len(ci) else None
        good=got==exp and diff is not None and sorted(set(i&~3 for i in diff))==sorted(sites)
        print('B transport member: %s; differs from the CI member in %s bytes = the %d intended words: %s'%('expected patched bytes' if got==exp else 'UNEXPECTED',len(diff) if diff is not None else '?',len(sites),good)); ok&=good
    cl=wifi_binpatch.clusters(ci)
    for i in cl:
        o=kern.count(ci[i:i+44]); want=sum(exp[j:j+44]==exp[i:i+44] for j in cl)   # NOP-ed clusters of the two prepare functions are byte-equal
        n=kern.count(exp[i:i+44]) if patched else None
        good=(o==0 and n==want) if patched else o==1
        print('B shape cluster @%#x: original %d x%s in kernel: %s'%(i,o,', NOP-ed %d x (want %d)'%(n,want) if patched else '',good)); ok&=good
    for obj,syms in (('wifi_normal_transport.o',['_terminal','_nct_run','_nct_continue_prepare','_nct_scan_return_prepare','_anchor','_seal','_basic_receipt','_pump']),('wifi_normal_rx.o',['_source','_ncr_pump','_ncr_continue_prepare','_prepare'])):
        res=[]
        for s in syms:
            best=runs(d+'/'+obj,s); n=kern.count(best); res.append('%s %d/%dx'%(s,len(best)//4,n)); ok&=(n==1 and len(best)>=12)
        print('%s %s (longest relocation-free run insns / occurrences in kernel): %s'%('B' if 'transport' in obj else 'A',obj,' '.join(res)))
a,b=text(C/'k/kernel/sweet_touch.o'),text(Rf/'k/kernel/sweet_touch.o')
if not patched:
    print('C sweet_touch.o __text candidate %s reference %s'%(a[:16],b[:16])); ok&=a==b
else:
    fa,fb=funcs(C/'k/kernel/sweet_touch.o'),funcs(Rf/'k/kernel/sweet_touch.o')
    changed=sorted(k for k in set(fa)|set(fb) if fa.get(k)!=fb.get(k)); bounds=[k for k in changed if bound_only(fa.get(k),fb.get(k))]
    changed=[k for k in changed if k not in bounds]; bad=[k for k in changed if k not in ALLOWED]
    print('C sweet_touch.o: %d functions; identical apart from an object-size bound constant: %s'%(len(fa),' '.join(bounds) or 'none'))
    print('C rebuilt binder functions (differ from the reference or new): %s'%' '.join(changed))
    print('C outside the allowed binder set: %s'%(' '.join(bad) or 'none')); ok&=not bad and '_nss_owner_loop' in changed
print('SCANPATH_OK' if ok else 'SCANPATH_MISMATCH'); sys.exit(0 if ok else 1)
