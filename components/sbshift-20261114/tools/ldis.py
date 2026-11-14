import sys, macho
macho.load(sys.argv[1]); sys.argv=[sys.argv[0]]+sys.argv[2:]
exec(open('dis.py').read())
