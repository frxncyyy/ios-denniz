#!/usr/bin/env python3
"""Authoritative iOS 16.5 Metal protocol selector inventory vs the Tegu frontend.

Parses the iPhoneOS16.5.sdk Metal headers (protocol bodies + properties), keeps what is
available on iOS <= 16.5, maps each protocol to the Tegu class that must implement it and
reports implemented / missing selectors (selector presence in the frontend sources, per class
including categories).  No device, no network.
Usage: api_inventory.py [--json OUT]
"""
import re,sys,json,glob,os,collections
HERE=os.path.dirname(os.path.abspath(__file__))
G=os.path.abspath(os.path.join(HERE,'..','..'))
SDK=os.path.join(G,'build/apple-toolchain/sdks/iPhoneOS16.5.sdk/System/Library/Frameworks/Metal.framework/Headers')
sys.path.insert(0,HERE)
import selscan
CLASS_OF={
 'MTLDevice':['TeguComputeDevice'],'MTLCommandQueue':['TeguQueue'],'MTLCommandBuffer':['TeguCommand'],
 'MTLCommandEncoder':['TeguEncoder','TeguRenderEncoder','TeguBlitEncoder'],
 'MTLComputeCommandEncoder':['TeguEncoder'],'MTLRenderCommandEncoder':['TeguRenderEncoder'],
 'MTLBlitCommandEncoder':['TeguBlitEncoder'],'MTLParallelRenderCommandEncoder':['TeguParallelRenderEncoder'],
 'MTLResourceStateCommandEncoder':['TeguResourceStateEncoder'],
 'MTLResource':['TeguBuffer','TeguTexture'],'MTLBuffer':['TeguBuffer'],'MTLTexture':['TeguTexture'],
 'MTLHeap':['TeguHeap'],'MTLFence':['TeguFence'],'MTLEvent':['TeguEvent'],'MTLSharedEvent':['TeguSharedEvent'],
 'MTLLibrary':['TeguLibrary'],'MTLFunction':['TeguFunction'],
 'MTLRenderPipelineState':['TeguRenderPipeline'],'MTLComputePipelineState':['TeguPipeline'],
 'MTLSamplerState':['TeguSampler'],'MTLDepthStencilState':['TeguDepthStencilState'],
 'MTLArgumentEncoder':['TeguArgumentEncoder'],'MTLIndirectCommandBuffer':['TeguIndirectCommandBuffer'],
 'MTLIndirectRenderCommand':['TeguIndirectRenderCommand'],'MTLIndirectComputeCommand':['TeguIndirectComputeCommand'],
 'MTLBinaryArchive':['TeguBinaryArchive'],'MTLCounterSampleBuffer':['TeguCounterSampleBuffer'],
 'MTLCounterSet':['TeguCounterSet'],'MTLCounter':['TeguCounter'],'MTLRasterizationRateMap':['TeguRasterizationRateMap'],
 'MTLDynamicLibrary':['TeguDynamicLibrary'],'MTLFunctionHandle':['TeguFunctionHandle'],
 'MTLVisibleFunctionTable':['TeguVisibleFunctionTable'],'MTLIntersectionFunctionTable':['TeguIntersectionFunctionTable'],
 'MTLAccelerationStructure':['TeguAccelerationStructure'],'MTLAccelerationStructureCommandEncoder':['TeguAccelerationStructureEncoder'],
 'MTLCaptureScope':['TeguCaptureScope'],'MTLDrawable':[],'MTLIOCommandQueue':['TeguIOCommandQueue'],
 'MTLIOCommandBuffer':['TeguIOCommandBuffer'],'MTLIOFileHandle':['TeguIOFileHandle'],
 'MTLLogContainer':[],'MTLFunctionLog':[],'MTLFunctionLogDebugLocation':[],
 'MTLSharedEventHandleX':[],
}
def ios_ok(attrs):
    """True if the entity exists on iOS <= 16.5."""
    if re.search(r'API_UNAVAILABLE\([^)]*ios',attrs): return False
    m=re.search(r'ios\(([\d.]+)\)',attrs)
    if m: return float(m.group(1))<=16.5
    if re.search(r'API_AVAILABLE\(',attrs) and 'macos' in attrs and 'ios' not in attrs: return False
    return True
def strip_comments(t):
    t=re.sub(r'/\*.*?\*/','',t,flags=re.S); return re.sub(r'//[^\n]*','',t)
def selector_of(decl):
    d=re.sub(r'\s+',' ',decl.strip().rstrip(';'))
    d=re.sub(r'API_\w+\((?:[^()]|\([^()]*\))*\)','',d); d=re.sub(r'MTL_\w+','',d); d=re.sub(r'NS_\w+(\([^)]*\))?','',d)
    m=re.match(r'[-+]\s*\(',d)
    if not m: return None
    i=m.end()-1;depth=0;j=i
    while j<len(d):
        if d[j]=='(':depth+=1
        elif d[j]==')':
            depth-=1
            if depth==0:break
        j+=1
    rest=d[j+1:].strip()
    parts=re.findall(r'([A-Za-z_]\w*)\s*:',rest)
    if parts: return ''.join(p+':' for p in parts)
    m=re.match(r'([A-Za-z_]\w*)',rest); return m.group(1) if m else None
def protocol_members(text):
    """yield (protocol, [(selector,kind,attrs)])"""
    t=strip_comments(text)
    for m in re.finditer(r'@protocol\s+(\w+)\b(?!\s*[;,])\s*(?:<[^>]*>)?\s*(.*?)\n\s*@end',t,re.S):
        name,body=m.group(1),m.group(2)
        out=[]
        for s in re.finditer(r'(@property\b[^;]*;|[-+]\s*\([^;]*;)',body,re.S):
            decl=s.group(1)
            attrs=' '.join(re.findall(r'API_\w+\((?:[^()]|\([^()]*\))*\)',decl))
            if decl.startswith('@property'):
                opts=re.match(r'@property\s*\(([^)]*)\)',decl)
                optlist=[re.sub(r'\s*=\s*','=',x.strip()) for x in opts.group(1).split(',')] if opts else []
                nm=re.search(r'(\w+)\s*(?:API_\w+\((?:[^()]|\([^()]*\))*\)\s*)*;$',decl.strip())
                nm=re.search(r'(\w+)\s*(?:\[[^\]]*\])?\s*(?:API_\w+\((?:[^()]|\([^()]*\))*\)|MTL_\w+|NS_\w+)*\s*;?$',re.sub(r'API_\w+\((?:[^()]|\([^()]*\))*\)','',decl).strip().rstrip(';').strip())
                if not nm: continue
                prop=nm.group(1)
                getter=[o.split('=')[1] for o in optlist if o.startswith('getter=')]
                setter=[o.split('=')[1] for o in optlist if o.startswith('setter=')]
                g=getter[0] if getter else prop
                out.append((g,'getter',attrs))
                if 'readonly' not in optlist:
                    out.append((setter[0] if setter else 'set'+prop[0].upper()+prop[1:]+':','setter',attrs))
            else:
                sel=selector_of(decl)
                if sel: out.append((sel,'method',attrs))
        yield name,out
def tegu_sources():
    d=os.path.join(G,'metal/tegu')
    files=[os.path.join(d,'compute_frontend.m'),os.path.join(d,'depth_stencil.m')]
    skip=('host_test','probe')
    files+= [f for f in sorted(glob.glob(os.path.join(d,'*.inc'))) if not any(s in f for s in skip)]
    return files
REFUSED=set()
def implemented():
    by=collections.defaultdict(set)
    for f in tegu_sources():
        for c,s,k,p,w,st in selscan.scan(f):
            if w=='impl' and c:
                by[c].add(s)
                if st=='refuse': REFUSED.add((c,s))
    return by
def main():
    impl=implemented()
    # NSObject base provides these on every class
    nsobj={'device','label','setLabel:'}
    res=collections.OrderedDict()
    for h in sorted(glob.glob(os.path.join(SDK,'*.h'))):
        for name,members in protocol_members(open(h,errors='replace').read()):
            if name not in CLASS_OF: continue
            classes=CLASS_OF[name]
            rows=[]
            for sel,kind,attrs in members:
                if not ios_ok(attrs): continue
                have=[c for c in classes if sel in impl.get(c,())]
                ref=bool(have) and all((c,sel) in REFUSED for c in have)
                rows.append({'selector':sel,'kind':kind,'implemented_by':have,'classes':classes,'status':'MISSING' if not have else 'REFUSE' if ref else 'IMPL'})
            res.setdefault(name,{'header':os.path.basename(h),'classes':classes,'rows':[]})['rows']+=rows
    return res
if __name__=='__main__':
    r=main()
    if len(sys.argv)>2 and sys.argv[1]=='--json': json.dump(r,open(sys.argv[2],'w'),indent=1)
    tot=miss=0
    for name,v in r.items():
        rows=v['rows']
        n_i=sum(x['status']=='IMPL' for x in rows);n_r=sum(x['status']=='REFUSE' for x in rows);n_m=sum(x['status']=='MISSING' for x in rows)
        tot+=len(rows);miss+=n_m
        print(f"{name:42s} impl={n_i:3d} refuse={n_r:3d} missing={n_m:3d} of {len(rows):3d}  classes={','.join(v['classes']) or '-'}")
    print('TOTAL rows',tot,'missing',miss)
