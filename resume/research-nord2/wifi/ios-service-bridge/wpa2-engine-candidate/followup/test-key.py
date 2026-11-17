from pathlib import Path
import tempfile,subprocess
P=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='nwpk-') as td:
 subprocess.run(['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(P/'key_station.c'),str(P/'test-key.c'),'-o',td+'/test'],check=True);subprocess.run([td+'/test'],check=True)
