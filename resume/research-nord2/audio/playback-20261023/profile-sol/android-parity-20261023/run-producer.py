from pathlib import Path
import subprocess
P=Path(__file__).resolve().parent;R=next(p for p in P.parents if(p/'AGENTS.md').is_file());S=R/'shim/nord2-xnu';T=R/'tests/nord2-audio'
modules=[S/(n+'.c')for n in ['audio_hw','audio_afe','audio_id','audio_i2c8','audio_amp','audio_board']]
flags=['clang','-std=c11','-DNORD2','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-sanitize-recover=all']
for role in ['original','candidate']:
 dirs=[P/role,T,S];src=P/role/'audio_stream.c'
 subprocess.run(flags+['-I'+str(x)for x in dirs]+[str(P/'test-producer.c'),str(src)]+list(map(str,modules))+['-o',str(P/('test-'+role))],check=True)
 subprocess.run([str(P/('test-'+role)),str(P.parent/'tfa98xx.cnt'),'1'if role=='original'else'0'],check=True)
