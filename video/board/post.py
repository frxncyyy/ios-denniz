#!/usr/bin/env python3
import argparse,json,time,uuid
from pathlib import Path
p=argparse.ArgumentParser();s=p.add_subparsers(dest="cmd",required=True)
a=s.add_parser("post");a.add_argument("--from",dest="sender",required=True);a.add_argument("--to",default="all");a.add_argument("--subject",required=True);a.add_argument("--body",required=True)
a=s.add_parser("read");a.add_argument("--limit",type=int,default=30)
x=p.parse_args();root=Path(__file__).resolve().parent
if x.cmd=="post":
 d={"time":time.time(),"from":x.sender,"to":x.to,"subject":x.subject,"body":x.body}
 name=f"{time.time_ns()}-{x.sender}-{uuid.uuid4().hex[:8]}.json";out=root/"messages"/name;tmp=out.with_suffix(".tmp");tmp.write_text(json.dumps(d,indent=2));tmp.replace(out);print(out)
else:
 for f in sorted((root/"messages").glob("*.json"))[-x.limit:]:print(f.read_text())
 for f in sorted(root.glob("*.md")):
  if f.name!="README.md":print(f"\n---{f.name}---\n"+f.read_text())
