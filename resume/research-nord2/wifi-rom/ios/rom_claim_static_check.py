def verify(data):
 import adt
 root,end=adt.parse(data)
 assert end==len(data)
 chosen=[c for c in root.children if c.name=='chosen'];assert len(chosen)==1
 props=[v for k,f,v in chosen[0].props if k=='nord2-conn-claim'];assert props==[bytes(24)]
