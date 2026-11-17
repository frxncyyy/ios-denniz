#ifndef NIWS_TEST_XNU_H
#define NIWS_TEST_XNU_H
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cassert>
#include <map>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
using UInt32=uint32_t;using IOReturn=int;
constexpr int kIOReturnSuccess=0,kIOReturnNotReady=-1,kIOReturnNotPrivileged=-2,kIOReturnBadArgument=-3,kIOReturnUnsupported=-4,kIOReturnNoDevice=-5,kIOReturnNoSpace=-6,kIOReturnNotOpen=-7,kIOReturnNoMemory=-8,kIOReturnNoResources=-9;
static const char*kIOClientPrivilegeAdministrator="admin";
struct task{std::atomic<int> refs{1};bool admin=true;};using task_t=task*;
#define TASK_NULL nullptr
static thread_local task_t caller;
static task_t current_task(){return caller;}
static void task_reference(task_t t){t->refs++;}static void task_deallocate(task_t t){assert(t->refs>1);t->refs--;}
static int objects=0,locks=0,lockAttempts=0,failLockAt=0,propertyCalls=0,failPropertyAt=0,attachCalls=0,failAttachAt=0,startCalls=0,failStartAt=0;
struct IOLock{std::mutex mutex;};static IOLock*IOLockAlloc(){if(++lockAttempts==failLockAt)return nullptr;locks++;return new IOLock;}static void IOLockFree(IOLock*p){locks--;delete p;}static void IOLockLock(IOLock*p){p->mutex.lock();}static void IOLockUnlock(IOLock*p){p->mutex.unlock();}
class OSObject{std::atomic<int> refs{1};public:OSObject(){objects++;}virtual ~OSObject(){objects--;}virtual bool init(){return true;}virtual void free(){}void retain(){refs++;}void release(){assert(refs>0);if(!--refs){free();delete this;}}};
class OSNumber:public OSObject{uint64_t n;public:explicit OSNumber(uint64_t v):n(v){}uint64_t unsigned64BitValue(){return n;}};
class OSString:public OSObject{public:std::string s;explicit OSString(const char*v):s(v){}};
class OSData:public OSObject{std::vector<uint8_t> b;public:OSData(const void*p,size_t n):b((const uint8_t*)p,(const uint8_t*)p+n){}size_t getLength(){return b.size();}const void*getBytesNoCopy(){return b.data();}};
class OSDictionary:public OSObject{std::map<std::string,OSObject*> values;public:~OSDictionary(){for(auto&v:values)v.second->release();}OSObject*getObject(const char*k){auto i=values.find(k);return i==values.end()?nullptr:i->second;}void setObject(const char*k,OSObject*v){v->retain();auto*p=getObject(k);if(p)p->release();values[k]=v;}void removeObject(const char*k){auto i=values.find(k);if(i!=values.end()){i->second->release();values.erase(i);}}};
#define OSDynamicCast(t,p) dynamic_cast<t*>(p)
#define OSDeclareDefaultStructors(t) public:t()=default;
#define OSDefineMetaClassAndStructors(t,b)
class IOUserClient;
class IOService:public OSObject{bool inactive=false;uint64_t id;IOService*opened=nullptr;public:IOService(){static uint64_t next=100;id=next++;}virtual bool start(IOService*){return ++startCalls!=failStartAt;}virtual void stop(IOService*){}bool attach(IOService*){return ++attachCalls!=failAttachAt;}void detach(IOService*){}bool isInactive(){return inactive;}void terminate(){inactive=true;}uint64_t getRegistryEntryID(){return id;}bool setProperty(const char*,const char*){return ++propertyCalls!=failPropertyAt;}void setName(const char*){}bool open(IOService*p){if(inactive||opened)return false;opened=p;return true;}void close(IOService*p){assert(opened==p);opened=nullptr;}virtual IOReturn setProperties(OSObject*){return kIOReturnUnsupported;}virtual IOReturn newUserClient(task_t,void*,UInt32,OSDictionary*,IOUserClient**){return kIOReturnUnsupported;}};
class IOUserClient:public IOService{public:static IOReturn clientHasPrivilege(void*p,const char*){auto*t=(task_t)p;return t&&t->admin?kIOReturnSuccess:kIOReturnNotPrivileged;}virtual bool initWithTask(task_t,void*,UInt32,OSDictionary*){return true;}virtual IOReturn clientClose(){return 0;}virtual IOReturn clientDied(){return clientClose();}};
struct ifnet{const char*prefix="en";unsigned unit=0;int refs=1;bool attached=true;};using ifnet_t=ifnet*;using errno_t=int;
static std::map<std::string,ifnet_t> table;
static const char*ifnet_name(ifnet_t n){return n->prefix;}static unsigned ifnet_unit(ifnet_t n){return n->unit;}
static errno_t ifnet_find_by_name(const char*s,ifnet_t*out){*out=nullptr;auto i=table.find(s);if(i==table.end()||!i->second->attached)return 1;*out=i->second;(*out)->refs++;return 0;}
static void ifnet_release(ifnet_t n){assert(n->refs>1);n->refs--;}
#endif
