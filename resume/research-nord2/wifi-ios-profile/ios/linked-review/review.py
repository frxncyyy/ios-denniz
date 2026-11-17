#!/usr/bin/env python3
"""Bounded exact-artifact extraction for scan-profile delta; never grants gates.
No arguments prepares a pending evidence inventory. Artifact mode requires the
root-verified expected hash, size and successful matching CI metadata.
"""
from pathlib import Path
import argparse,hashlib,json,re,struct,subprocess

P=Path(__file__).resolve().parent
R=next(p for p in P.parents if (p/'shim/nord2-xnu').is_dir())
T=R.parent/'toolchains/proton-clang/bin'
HEAD='27717fffcbbd0221334a4b18df6565551f877ab3'
CI_RUN=39750474116
BASE=R/'research/nord2/wifi-ios-compat/ios/linked-review'
SOURCES=['shim/nord2-xnu/wifi_ios_policy.c','shim/nord2-xnu/wifi_ios_policy.h',
         'shim/nord2-xnu/wifi_ios_client.cpp']
FUNCTIONS=[('scan_policy','_niwi_scan_admit',True,'wifi_ios_policy'),
           ('power_policy','_niwi_power_on_admit',True,'wifi_ios_policy'),
           ('client_scan','NiwlLifecycleClient::scanLocked(',False,'wifi_ios_client'),
           ('client_dispatch','NiwlLifecycleClient::dispatchLocked(',False,'wifi_ios_client')]

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def save(name,data):
    path=P/name;path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(data,indent=2)+'\n')
def run(*argv):return subprocess.check_output(list(map(str,argv)),text=True)

class MachO:
    def __init__(self,path):
        self.path=path;self.data=path.read_bytes();self.segments=[];self.sections=[]
        assert len(self.data)>=32 and struct.unpack_from('<I',self.data)[0]==0xfeedfacf
        ncmds,sizeofcmds=struct.unpack_from('<II',self.data,16)
        assert ncmds<4096 and 32+sizeofcmds<=len(self.data)
        off=32
        for _ in range(ncmds):
            cmd,n=struct.unpack_from('<II',self.data,off)
            assert n>=8 and off+n<=32+sizeofcmds
            if cmd==0x19:
                va,vs,fo,fs=struct.unpack_from('<QQQQ',self.data,off+24)
                assert fo+fs<=len(self.data)
                self.segments.append((va,fo,fs))
                ns=struct.unpack_from('<I',self.data,off+64)[0]
                assert 72+80*ns<=n
                for i in range(ns):
                    s=off+72+i*80
                    name=self.data[s:s+16].split(b'\0')[0].decode()
                    a,z=struct.unpack_from('<QQ',self.data,s+32)
                    fileoff=struct.unpack_from('<I',self.data,s+48)[0]
                    self.sections.append((name,a,z,fileoff))
            off+=n
        self.nm=run(T/'llvm-nm','-n','--demangle',path)
        self.syms=[]
        for line in self.nm.splitlines():
            q=line.split(maxsplit=2)
            if len(q)!=3:continue
            try:self.syms.append((int(q[0],16),q[1],q[2]))
            except ValueError:pass
    def text(self,a):return any(n=='__text' and v<=a<v+z for n,v,z,_ in self.sections)
    def bounds(self,pattern,exact):
        hits={(a,n) for a,_,n in self.syms if self.text(a) and (n==pattern if exact else n.startswith(pattern))}
        assert len(hits)==1,(pattern,hits)
        a,n=hits.pop()
        section_end=min(v+z for s,v,z,_ in self.sections if s=='__text' and v<=a<v+z)
        end=min([section_end]+[v for v,_,_ in self.syms if a<v<section_end and self.text(v)])
        assert 0<end-a<65536 and (end-a)%4==0
        return a,end,n
    def read(self,a,n):
        # Section offsets handle relocatable MH_OBJECT's zero-based segment too.
        hits=[(v,f) for _,v,z,f in self.sections if v<=a and a-v+n<=z]
        assert len(hits)==1,(hex(a),n,hits)
        v,f=hits[0];assert f+a-v+n<=len(self.data)
        return self.data[f+a-v:f+a-v+n]
    def dump(self,a,end):
        return run(T/'llvm-objdump','-d','--mattr=+lse','--no-show-raw-insn','--demangle',
                   '--start-address='+hex(a),'--stop-address='+hex(end),self.path)

def prepare():
    assert run('git','-C',R,'rev-parse','HEAD').strip()==HEAD
    sources={x:sha(R/x) for x in SOURCES}
    for x,h in sources.items():assert hashlib.sha256(subprocess.check_output(['git','-C',str(R),'show',HEAD+':'+x])).hexdigest()==h,x
    diff=run('git','-C',R,'show','--format=fuller',HEAD,'--',*SOURCES)
    (P/'source-delta.patch').write_text(diff)
    records=json.loads((P.parent/'focused/commands.json').read_text())
    focused={}
    for name in ['wifi_ios_policy','wifi_ios_client']:
        record=next(x for x in records if x['name']==name)
        suffix='.c' if name.endswith('policy') else '.cpp'
        canonical=R/'shim/nord2-xnu'/(name+suffix)
        generated=Path(next(x for x in record['argv'] if x.endswith('/'+name+suffix)))
        assert record['exit']==0 and record['source_sha256']==sha(generated)
        assert generated.read_bytes()==b'#include <arm64/sweet_early.h>\n#if defined(NORD2)\n'+canonical.read_bytes()+b'\n#endif /* NORD2 */\n'
        assert '-mcpu=generic' in record['argv'] and '-march=armv8.2-a+crypto' in record['argv']
        obj=P.parent/'focused'/(name+'.o');assert obj.is_file()
        focused[name]={'path':str(obj),'sha256':sha(obj),'generated_source_sha256':record['source_sha256'],
                       'canonical_source_sha256':sha(canonical),'generated_source_has_exact_canonical_body':True}
    # Reuse old review identity as context only; never promote its PASS to this kernel.
    old=json.loads((BASE/'report.json').read_text())
    assert old['identity']['source_commit']=='b7800f8d81a197a490923e6443157a1d740effbe'
    pending={'status':'PENDING_ARTIFACT','verdict':None,'expected_source':HEAD,'expected_ci_run':CI_RUN,
             'scope':'Only semantic scan/RSSI policy, ON helper, ADMISSION/POWERSET metadata and their direct caller branches',
             'sources':sources,'focused_objects':focused,
             'prior_linked_review':{'report_sha256':sha(BASE/'report.json'),'manifest_sha256':sha(BASE/'manifest.json'),'kernel_sha256':old['identity']['kernel_sha256']},
             'not_repeated':['registry','owner','DMA','event/result pipeline','whole-kernel ISA audit'],
             'prepared_image_gate':False,'device_gate':False}
    save('pending.json',pending)
    return pending

def extract(args,pending):
    assert args.kernel and args.run_json and args.ci_run and args.sha256 and args.bytes
    k=Path(args.kernel).resolve();rj=Path(args.run_json).resolve()
    assert k.is_file() and rj.is_file() and len(args.sha256)==64
    metadata=json.loads(rj.read_text())
    assert metadata['databaseId']==args.ci_run==CI_RUN and metadata['conclusion']=='success' and metadata['headSha']==HEAD
    assert k.stat().st_size==args.bytes and sha(k)==args.sha256
    linked=MachO(k);out=P/'artifact';out.mkdir(exist_ok=True)
    extracts={};objects={}
    for label,pattern,exact,unit in FUNCTIONS:
        a,end,sym=linked.bounds(pattern,exact);body=linked.read(a,end-a)
        path=out/(label+'.txt');path.write_text(linked.dump(a,end))
        objmeta=pending['focused_objects'][unit];objpath=Path(objmeta['path'])
        assert sha(objpath)==objmeta['sha256']
        obj=objects.setdefault(unit,MachO(objpath))
        oa,oe,osym=obj.bounds(pattern,exact);ob=obj.read(oa,oe-oa)
        (out/(label+'-object.txt')).write_text(obj.dump(oa,oe))
        row={'symbol':sym,'address':hex(a),'end':hex(end),'bytes':end-a,
             'code_sha256':hashlib.sha256(body).hexdigest(),'disassembly_sha256':sha(path),
             'native_object_address':hex(oa),'native_object_bytes':len(ob),
             'exact_native_object_bytes_equal':body==ob,'object_sha256':objmeta['sha256']}
        if label in ('scan_policy','power_policy'):
            # These pure helpers contain no relocations; equality is substantive.
            relocs=run(T/'llvm-objdump','-r',objpath)
            selected=[];in_text=False
            for line in relocs.splitlines():
                section=re.match(r'^RELOCATION RECORDS FOR \[(.+)\]:',line)
                if section:in_text=section[1]=='__text'
                m=re.match(r'^([0-9a-fA-F]+)\s+(ARM64_RELOC_\S+)\s+(.*)$',line)
                if in_text and m and oa<=int(m[1],16)<oe:selected.append(line)
            assert not selected,(label,selected)
            row['relocations_in_pure_function']=[]
            row['pure_helper_exact_link_check']='PASS' if body==ob else 'MISMATCH_REQUIRES_REVIEW'
        extracts[label]=row
    # Record bounded full strings, their mapped VA and actual bytes; no private data.
    strings={}
    for label,prefix in [('ADMISSION',b'NORD2 WIFI IOS ADMISSION:'),('POWERSET',b'NORD2 WIFI IOS POWERSET:'),('SCANREQ',b'NORD2 WIFI IOS SCANREQ:')]:
        hits=[]
        for section,va,z,fo in linked.sections:
            if section!='__cstring':continue
            sectionbytes=linked.data[fo:fo+z];start=0
            while True:
                at=sectionbytes.find(prefix,start)
                if at<0:break
                stop=sectionbytes.find(b'\0',at);assert at<stop<=at+2048
                hits.append({'address':hex(va+at),'string':sectionbytes[at:stop].decode(),'section':section})
                start=stop+1
        assert len(hits)==1,(label,hits);strings[label]=hits[0]
    result={'status':'EXTRACTED_PENDING_MANUAL_REVIEW','verdict':None,
            'identity':{'ci_run':args.ci_run,'source_commit':HEAD,'kernel':str(k),
                        'bytes':args.bytes,'sha256':args.sha256,'run_json_sha256':sha(rj)},
            'functions':extracts,'log_strings':strings,'sources':pending['sources'],
            'prepared_image_gate':False,'device_gate':False}
    save('artifact/extraction.json',result)
    print('EXTRACTED: four bounded functions; verdict remains pending manual linked branch review')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kernel');parser.add_argument('--run-json');parser.add_argument('--ci-run',type=int)
    parser.add_argument('--sha256');parser.add_argument('--bytes',type=int)
    args=parser.parse_args();pending=prepare()
    if any(vars(args).values()):extract(args,pending)
    else:print('PENDING_ARTIFACT: source pins and focused review method prepared; no linked verdict or gate')
if __name__=='__main__':main()
