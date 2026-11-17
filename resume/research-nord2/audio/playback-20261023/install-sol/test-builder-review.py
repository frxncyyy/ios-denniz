#!/usr/bin/env python3
"""Builder orchestration test only: fake SDK/tool outputs; never compiles a HAL."""
import contextlib,importlib.util,io,json,pathlib,struct,sys,tempfile,types
from unittest.mock import patch
HERE=pathlib.Path(__file__).resolve().parent;ROOT=HERE.parents[4]
def load(name,p):
 s=importlib.util.spec_from_file_location(name,p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
b=load('builder',HERE/'build-audio-plugin-review-proposal.py');pkg=load('pkg',ROOT/'tools/nord2-audio-package.py')
sources={n:(HERE.parent/'upstream/hackios-userland/userland/src'/n).read_bytes() for n in b.FILES}
sources['LICENSE']=(HERE/'LICENSE.upstream-userland').read_bytes()
for scenario in ['success','source-corruption','license-corruption','simulator','wrong-minos','signature']:
 commands=[]
 def urlopen(url,**kwargs):
  if hasattr(url,'full_url'):url=url.full_url
  name=url.rsplit('/',1)[-1];data=sources[name]
  if scenario=='source-corruption' and name=='audio_plugin.c':data+=b'corrupt'
  if scenario=='license-corruption' and name=='LICENSE':data+=b'corrupt'
  return io.BytesIO(data)
 def check_output(args,**kwargs):
  commands.append(args)
  if '--show-sdk-path' in args:return '/fake/iPhoneOS17.5.sdk\n'
  if '--show-sdk-version' in args:return '17.5\n'
  if '-l' in args:return 'cmd LC_CODE_SIGNATURE\n' if scenario=='signature' else 'cmd LC_BUILD_VERSION\n'
  raise AssertionError(args)
 def run(args,**kwargs):
  commands.append(args)
  if 'clang' in args:
   assert args[args.index('-isysroot')+1]=='/fake/iPhoneOS17.5.sdk'
   assert '-miphoneos-version-min=16.0' in args and '-Wl,-no_adhoc_codesign' in args
   pathlib.Path(args[args.index('-o')+1]).write_bytes(struct.pack('<IIIIIIII',0xfeedfacf,0x100000c,0,8,0,0,0,0))
  if 'vtool' in args:
   output='platform '+('IOSSIMULATOR' if scenario=='simulator' else 'IOS')+'\nminos '+('17.0' if scenario=='wrong-minos' else '16.0')+'\n'
  elif 'nm' in args:output='00000000 T _RedwoodAudio_Create\n'
  elif '-hv' in args:output='BUNDLE\n'
  else:output=''
  if args[0]=='python3':
   d=pathlib.Path(args[-2]);pathlib.Path(args[-1]).write_bytes(pkg.encode((d/'RedwoodAudio').read_bytes(),(d/'Info.plist').read_bytes()))
  return types.SimpleNamespace(stdout=output)
 with tempfile.TemporaryDirectory() as t,patch.object(sys,'argv',['builder',str(pathlib.Path(t)/'out')]),patch.object(b.urllib.request,'urlopen',urlopen),patch.object(b.subprocess,'run',run),patch.object(b.subprocess,'check_output',check_output),contextlib.redirect_stdout(io.StringIO()):
  try:b.main()
  except SystemExit:
   assert scenario!='success',scenario
  else:
   assert scenario=='success',scenario
   out=pathlib.Path(t)/'out';assert (out/'LICENSE.upstream').read_bytes()==sources['LICENSE']
   record=json.loads((out/'provenance.json').read_text());assert record['sdk_version']=='17.5' and record['minimum_ios']=='16.0' and record['signature']=='none'
print('PASS builder orchestration mock: explicit SDK/device/minimum16/no-signature/license and5 rejection cases; no Apple SDK execution')
