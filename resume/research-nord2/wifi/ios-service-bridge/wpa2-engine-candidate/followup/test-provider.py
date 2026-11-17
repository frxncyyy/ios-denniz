from pathlib import Path
import subprocess,tempfile,argparse
P=Path(__file__).resolve().parent;R=next(x for x in P.parents if (x/'shim/nord2-xnu').is_dir())
a=argparse.ArgumentParser();a.add_argument('--bearssl',type=Path,default=R/'research/ci-39398655023/buildtree/xnu/libkern/crypto/sweet_bearssl');b=a.parse_args().bearssl
with tempfile.TemporaryDirectory(prefix='nwpc-bearssl-') as td:
 files=['hash/sha1.c','codec/dec32be.c','codec/enc32be.c','symcipher/aes_ct.c','symcipher/aes_ct_dec.c','symcipher/aes_ct_cbcdec.c']
 cmd=['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(b/'inc'),'-I'+str(b/'src'),str(P/'test-provider.c'),str(P/'crypto_provider.c')]+[str(b/'src'/x) for x in files]+['-o',td+'/test'];subprocess.run(cmd,check=True);subprocess.run([td+'/test'],check=True)
