import struct, sys, re
exec(open(sys.argv[0].replace('stubsel.py','common.py')).read())
dis = open(sys.argv[2]).read().splitlines()
idx = {l.split(':')[0].strip(): i for i, l in enumerate(dis) if re.match(r'^ *1000[0-9a-f]+:', l)}
for a in sys.argv[3:]:
    i = idx[a]
    page = int(re.search(r'adrp\s+x1, (0x[0-9a-f]+)', dis[i]).group(1), 16)
    off = int(re.search(r'#(0x[0-9a-f]+)\]', dis[i+1]).group(1), 16)
    v = struct.unpack_from('<Q', data, a2o(page+off))[0]
    print(a, cstr(ptr(v)))
