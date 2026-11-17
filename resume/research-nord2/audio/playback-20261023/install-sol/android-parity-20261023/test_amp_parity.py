#!/usr/bin/env python3
"""Compile real current/candidate source for cold reset, profile and allowlist tests."""
import argparse,subprocess,tempfile
from pathlib import Path
R=next(p for p in Path(__file__).resolve().parents if (p/'shim/nord2-xnu/audio_hw.c').exists())
D=Path(__file__).resolve().parent
p=argparse.ArgumentParser();p.add_argument('--source-dir',type=Path,default=R/'shim/nord2-xnu');a=p.parse_args();S=a.source_dir.resolve()
with tempfile.TemporaryDirectory(prefix='nord2-amp-parity-')as temp:
 for name,sources in [('test-i2cr.c',['audio_i2c8.c']),('test-amp-parity.c',['audio_amp.c','audio_board.c'])]:
  exe=Path(temp)/name
  subprocess.run(['clang','-DNORD2','-std=c11','-Wall','-Wextra','-Werror','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-include',str(S/'audio_i2c8.h'),'-I'+str(R/'shim/nord2-xnu'),'-I'+str(R/'tests/nord2-audio'),*[str(S/x)for x in sources],str(D/name),'-o',str(exe)],check=True)
  subprocess.run([str(exe),str(R/'research/nord2/audio/playback-20261023/profile-sol/tfa98xx.cnt')],check=True)
