#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile
P=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as d:
 for name,src in [('battery',['mtk_i2c_read.c','test-i2c.c']),('audio',['audio_id.c','audio_i2c8.c','test-audio.c'])]:
  out=Path(d)/name
  subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-DNORD2=1','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(P),*[str(P/x) for x in src],'-o',str(out)],check=True)
  subprocess.run([str(out)],check=True)
print('PASS battery and audio count-mask focused host fixtures; no hardware.')
