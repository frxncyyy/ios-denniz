#!/usr/bin/env python3
"""Extract (class, selector, kind, file) from ObjC sources, joining multi-line method headers.
Usage: selscan.py FILE...   (JSON on stdout)"""
import re,sys,json
HDR=re.compile(r'^\s*[-+]\s*\(')
def selector(text):
    # strip return type "(...)" with possible nested parens
    i=text.index('(');depth=0;j=i
    while j<len(text):
        if text[j]=='(':depth+=1
        elif text[j]==')':
            depth-=1
            if depth==0:break
        j+=1
    rest=text[j+1:].strip()
    # cut at first '{' or ';'
    rest=re.split(r'[{;]',rest,1)[0]
    # remove arg types "(...)name"
    out=[];k=0
    parts=re.findall(r'([A-Za-z_]\w*)\s*:\s*(?:\((?:[^()]|\([^()]*\))*\))?\s*\w*',rest)
    if ':' in rest and parts: return ''.join(p+':' for p in parts)
    m=re.match(r'\s*([A-Za-z_]\w*)',rest)
    return m.group(1) if m else None
def scan(path):
    cls=None;out=[];lines=open(path,errors='replace').read().split('\n');i=0
    while i<len(lines):
        line=lines[i]
        m=re.match(r'^@(?:implementation|interface)\s+(\w+)',line)
        if m: cls=m.group(1)
        if re.match(r'^@end',line): cls=None
        mq=re.match(r'^\s*TEGU_API_QUERY\((\w+),',line)
        if mq: out.append((cls,mq.group(1),'-',path,'impl','')) # capability getter macro (always a constant answer)
        if HDR.match(line):
            text=line;n=i
            while not re.search(r'[{;]',text) and n+1<len(lines) and n-i<6:
                n+=1;text+=' '+lines[n].strip()
            s=selector(text)
            if s:
                kind='impl' if '{' in text.split(';')[0] else 'decl'
                status=''
                if kind=='impl':
                    # body = until the next method header / @end
                    k=n+1;body=text
                    while k<len(lines) and not HDR.match(lines[k]) and not lines[k].startswith('@end') and k-n<60:
                        body+='\n'+lines[k];k+=1
                    if re.search(r'TEGU_API_REFUSE|tegu_api_unsupported|selector_refused|UNSUPPORTED',body): status='refuse'
                out.append((cls,s,line.strip()[0],path,kind,status))
        i+=1
    return out
if __name__=='__main__':
    res=[]
    for p in sys.argv[1:]: res+=scan(p)
    json.dump(res,sys.stdout)
