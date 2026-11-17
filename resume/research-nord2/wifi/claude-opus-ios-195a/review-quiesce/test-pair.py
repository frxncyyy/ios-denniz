from pathlib import Path
import subprocess,tempfile,json
P=Path(__file__).resolve().parent

def function(file,signature):
 t=(P/'proposed/iokit/Kernel'/file).read_text();i=t.index(signature);a=t.index('{',i);depth=1;j=a+1
 while depth:
  depth+=(t[j]=='{')-(t[j]=='}');j+=1
 return t[i:j]
start=function('IOStartIOKit.cpp','void\nStartIOKitMatching(void)')
ready=function('IOService.cpp','void\nIOService::iokitDaemonLaunched(void)')
pre='''#include <cstddef>
#include <cstdint>
#include <cassert>
#define START_IOKIT 0
#define SOCD_TRACE_XNU(x) ((void)0)
#define IOServiceTrace(...) ((void)0)
#define IOSERVICE_KEXTD_READY 0
#define __improbable(x) (x)
#define kIOKitDaemonName "daemon"
static void panic(const char*){assert(false);}
static void nord2_stop_at(unsigned,const char*){}
static void IOLog(const char*,...){}
static void clock_get_uptime(uint64_t*p){*p=123456;}
static void absolutetime_to_nanoseconds(uint64_t t,uint64_t*p){*p=t;}
static unsigned rematches,publications,starts;
class IOService{public:
 int busy=5;unsigned inc=0,dec=0;
 static IOService*getServiceRoot();
 void adjustBusy(int x){busy+=x;if(x>0)inc++;else dec++;}
 unsigned getBusyState(){return busy;}
 bool startIOServiceMatching(){starts++;return true;}
 static void startDeferredMatches(){rematches++;}
 static void publishUserResource(const char*){publications++;}
 static void iokitDaemonLaunched();};
static IOService root;
static IOService*gRootNub=&root;
IOService*IOService::getServiceRoot(){return &root;}
class OSKext{public:static bool available;static bool iokitDaemonAvailable(){return available;}};
bool OSKext::available=true;
static const char*gIOResourceIOKitKey="IOKit";
'''
post='''
int main(){
 for(unsigned available=0;available<2;available++){
 root=IOService();rematches=publications=starts=0;OSKext::available=available;
 StartIOKitMatching();assert(starts==1);
#if !NO_KEXTD && !defined(NORD2)
 assert(root.busy==5+(int)available&&root.inc==available);
#else
 assert(root.busy==5&&!root.inc);
#endif
 if(available){IOService::iokitDaemonLaunched();assert(root.busy==5);
#if NO_KEXTD
 assert(!rematches&&!publications&&!root.dec);
#else
 assert(rematches==1&&publications==1);
#if defined(NORD2)
 assert(!root.dec);
#else
 assert(root.dec==1);
#endif
#endif
 }
 }
}
'''
records=[]
with tempfile.TemporaryDirectory(prefix='quiesce-pair-')as td:
 d=Path(td);f=d/'test.cpp';f.write_text(pre+start+'\n'+ready+post)
 for nord2 in (0,1):
  for no in (0,1):
   cmd=['clang++','-std=c++17','-fsanitize=address,undefined','-fno-sanitize-recover=all',f'-DNO_KEXTD={no}',*(['-DNORD2']if nord2 else[]),str(f),'-o',str(d/'test')]
   subprocess.run(cmd,check=True);subprocess.run([str(d/'test')],check=True);records.append({'NORD2':nord2,'NO_KEXTD':no,'availability_cases':2,'status':'PASS'})
(P/'pair-test.json').write_text(json.dumps({'scope':'Extracted proposed function bodies; dependencies modeled, not real IOKit execution','variants':records},indent=2)+'\n')
print('PASS 4 preprocessor variants x 2 availability cases; exact paired hold/release, retained unrelated5counts and deferred/resource behavior; modeled dependencies')
