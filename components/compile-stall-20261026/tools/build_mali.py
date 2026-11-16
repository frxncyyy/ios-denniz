#!/usr/bin/env python3
"""Private host libnord2_mali_runtime.so from the given source dir (shared build's flags, Mesa archives reused).
Usage: build_mali.py SRCDIR OUTDIR"""
import json,subprocess,sys,shlex,os
G='$NORD2_WORK/gpu'
B=G+'/build/mesa-build'
src,out=os.path.abspath(sys.argv[1]),os.path.abspath(sys.argv[2])
os.makedirs(out,exist_ok=True)
cc=json.load(open(B+'/compile_commands.json'))
objs=[]
for name in ('mali_compute_compiler.c','mali_graphics_compiler.c'):
    e=[e for e in cc if e['file'].endswith('/clc/'+name) and 'libnord2_mali_runtime' in e['command']][0]
    a=shlex.split(e['command']);clean=[];skip=False
    for x in a:
        if skip:skip=False;continue
        if x in('-MQ','-MF'):skip=True;continue
        if x=='-MD':continue
        clean.append(x)
    o=os.path.join(out,name+'.o');clean[clean.index('-o')+1]=o;clean[clean.index('-c')+1]=os.path.join(src,name)
    subprocess.run(['nice','-n','10']+clean,cwd=B,check=True);objs.append(o)
link=subprocess.check_output(['ninja','-t','commands','src/panfrost/clc/libnord2_mali_runtime.so'],cwd=B,text=True).strip().splitlines()[-1]
a=shlex.split(link);a[a.index('-o')+1]=os.path.join(out,'libnord2_mali_runtime.so')
a=[objs[0] if x.endswith('mali_compute_compiler.c.o') else objs[1] if x.endswith('mali_graphics_compiler.c.o') else x for x in a]
subprocess.run(['nice','-n','10']+a,cwd=B,check=True)
print('built',os.path.join(out,'libnord2_mali_runtime.so'))
