#!/usr/bin/env python3
"""Reconstruct exactly one CI echo split by an unrelated parallel iig echo."""
from pathlib import Path
import hashlib,json,re,shlex
P=Path(__file__).resolve().parent;R=next(p for p in P.parents if (p/'AGENTS.md').exists());C=R/'research/ci-39768467149';raw=(C/'build.log').read_bytes();lines=raw.decode().splitlines();rows=[]
for i,l in enumerate(lines,1):
 if ' -MF sweet_touch.d ' in l and ' -c ' in l:
  start=l.index('/Applications/');foreign=l.find('/Applications/',start+1);assert foreign>start
  prefix=l[start:foreign];assert prefix.endswith('-I/Users/ru')
  tails=[(j,t)for j,t in enumerate(lines[i:i+8],i+1)if '/xnu/bsd/dev/sweet_touch.c' in t]
  assert len(tails)==1;tailno,tail=tails[0];tail=tail.split('Z ',1)[1];assert tail.startswith('nner/work/')
  command=prefix+tail;args=shlex.split(command);assert args.count('-mcpu=generic')==1 and args.count('-march=armv8.2-a+crypto')==1
  assert [a for a in args if a.startswith('-mcpu=')]==['-mcpu=generic'];assert [a for a in args if a.startswith('-march=')]==['-march=armv8.2-a+crypto']
  assert args[-1]=='/Users/runner/work/ios-sweet/ios-sweet/dxb/xnu/bsd/dev/sweet_touch.c'
  assert '-c' in args and args[0].endswith('/clang')
  rows.append({'first_line':i,'tail_line':tailno,'excluded_foreign_echo':'parallel iig OSAction.iig --header OSAction.h --impl OSAction.iig.cpp','prefix_exact':prefix,'tail_exact':tail,'reconstructed_command':command,'reconstructed_sha256':hashlib.sha256(command.encode()).hexdigest(),'mcpu':'generic','march':'armv8.2-a+crypto','method':'literal concatenation across observed /Users/ru + nner seam; no inferred flags or source association'})
assert len(rows)==1
# Match the audit's fixed target list; validate all ordinary complete echoes.
units=[(p.stem,p.suffix)for p in (R/'shim/nord2-xnu').glob('wifi_ios_*')if p.suffix in ('.c','.cpp')]+[(p,'.c')for p in ('wifi_normal_transport','wifi_normal_rx','wifi_vcn33','wifi_scan_bss')]+[('IOStartIOKit','.cpp'),('IOService','.cpp')]
audit={}
for name,ext in units:
 found=[(n,l)for n,l in enumerate(lines,1)if '/'+name+ext in l and ' -c ' in l and '-mcpu=' in l];assert found,name
 for n,l in found:
  assert '-mcpu=generic' in l and '-march=armv8.2-a+crypto' in l,(name,n)
 audit[name]={'compile_echo_lines':[n for n,_ in found],'generic':True}
audit['sweet_touch']={'reconstructed_lines':[rows[0]['first_line'],rows[0]['tail_line']],'generic':True}
result={'status':'PASS_BOUNDED_COMPILE_FLAGS_ADDENDUM','ci':39768467149,'source':'4e48740b66ca88d3d77778406b16391a55001f7c','build_log_sha256':hashlib.sha256(raw).hexdigest(),'sweet_touch_reconstruction':rows[0],'actual_target_units':audit,'limits':'Compiler echo proves flags associated with source; successful CI, source-tar equality and linked artifact independently supplied by root. Not proof of command execution from echo alone.'}
(P/'compile-flags-addendum.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS',len(audit),'actual target compile echoes; exact sweet_touch split lines',rows[0]['first_line'],rows[0]['tail_line'])
