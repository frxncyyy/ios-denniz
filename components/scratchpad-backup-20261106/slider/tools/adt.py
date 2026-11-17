import struct,sys
def parse(d,o=0):
    np_,nc=struct.unpack_from('<II',d,o); o+=8; props={}
    for _ in range(np_):
        name=d[o:o+32].split(b'\0')[0].decode(errors='replace'); ln=struct.unpack_from('<I',d,o+32)[0]; l=ln&0x7fffffff
        props[name]=d[o+36:o+36+l]; o+=36+((l+3)&~3)
    kids=[]
    for _ in range(nc):
        k,o=parse(d,o); kids.append(k)
    return (props,kids),o
def walk(n,path=''):
    p,k=n; name=p.get('name',b'').rstrip(b'\0').decode(errors='replace')
    full=path+'/'+name if path or name!='device-tree' else ''
    yield full or '/',n
    for c in k: yield from walk(c,full)
if __name__=='__main__':
    d=open(sys.argv[1],'rb').read(); root,_=parse(d)
    for path,(p,k) in walk(root):
        if any(x in path for x in sys.argv[2:]):
            print(path)
            for n,v in p.items(): print('   %-32s %s'%(n, v[:48].hex() if not all(32<=b<127 or b==0 for b in v) else v[:64]))
