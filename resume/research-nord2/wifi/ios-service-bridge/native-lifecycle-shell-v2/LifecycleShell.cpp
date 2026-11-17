/* Private, unregistered lifetime implementation. No IO80211 class identity,
 * personality, matching, registerService, ifnet, backend hook or event producer. */
#ifdef NIWL_HOST_TEST
#include "test-xnu.h"
#else
#include <IOKit/IOUserClient.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSBoolean.h>
#include <kern/task.h>
#endif
extern "C" {
#include "../bind-transport-policy/binding_contract.h"
#include "../control_contract.h"
#include "../event-pipe/pipe.h"
}
class NiwlLifecycleClient : public IOUserClient {
 OSDeclareDefaultStructors(NiwlLifecycleClient)
 task_t owner;IOLock *gate;
 enum Phase {Closed,Open,Allocated,Armed};Phase phase;
 IOBufferMemoryDescriptor *data,*state;
 OSAsyncReference64 reference;bool ownsReference;
 uint64_t generation;
 void discardLocked(void);
 void closeOwned(void);
 IOReturn dispatchLocked(uint32_t,IOExternalMethodArguments *,bool *);
public:
 bool initWithTask(task_t,void *,UInt32,OSDictionary *) override;
 IOReturn externalMethod(uint32_t,IOExternalMethodArguments *,IOExternalMethodDispatch *,OSObject *,void *) override;
 IOReturn clientMemoryForType(UInt32,IOOptionBits *,IOMemoryDescriptor **) override;
 IOReturn clientClose(void) override;
 IOReturn clientDied(void) override;
 void stop(IOService *) override;
 void free(void) override;
};
OSDefineMetaClassAndStructors(NiwlLifecycleClient,IOUserClient)
bool NiwlLifecycleClient::initWithTask(task_t task,void *security,UInt32 type,OSDictionary *props){
 owner=TASK_NULL;gate=nullptr;phase=Closed;data=state=nullptr;ownsReference=false;generation=0;bzero(reference,sizeof reference);
 if(!task||current_task()!=task||type)return false;
 OSObject *entitlement=IOUserClient::copyClientEntitlement(task,"com.apple.wifi.manager-access");
 OSBoolean *allowed=OSDynamicCast(OSBoolean,entitlement);bool authorized=allowed&&allowed->isTrue();
 if(entitlement)entitlement->release();
 if(!authorized||!IOUserClient::initWithTask(task,security,type,props))return false;
 gate=IOLockAlloc();if(!gate)return false;
 task_reference(task);owner=task;generation=1;phase=Open;return true;
}
void NiwlLifecycleClient::discardLocked(void){
 /* No producer or pending callbacks exists in this revision. Closing admission
  * before this function is therefore quiescent. Existing task mappings retain
  * their own descriptor refs; released generations are never written again. */
 if(ownsReference){IOUserClient::releaseAsyncReference64(reference);ownsReference=false;bzero(reference,sizeof reference);}
 if(data){data->release();data=nullptr;}if(state){state->release();state=nullptr;}
}
void NiwlLifecycleClient::closeOwned(void){
 if(!gate)return;IOLockLock(gate);phase=Closed;discardLocked();IOLockUnlock(gate);
}
IOReturn NiwlLifecycleClient::dispatchLocked(uint32_t selector,IOExternalMethodArguments*a,bool*adopted){
 if(phase==Closed)return kIOReturnNotOpen;
 if(current_task()!=owner)return kIOReturnNotPrivileged;
 if(!a||a->version!=kIOExternalMethodArgumentsCurrentVersion||a->selector!=selector||a->scalarInputCount||a->scalarOutputCount||a->structureOutputDescriptor)return kIOReturnBadArgument;
 if(selector==0||selector==6){
  if(a->asyncReferenceCount||a->asyncWakePort)return kIOReturnBadArgument;
  size_t bytes=selector==0?NIW_SMALL_BYTES:NIW_LARGE_BYTES;
  const void*carrier=a->structureInput;void*copy=nullptr;
  if(a->structureInputDescriptor){
   if(a->structureInputSize||a->structureInputDescriptor->getLength()!=bytes)return kIOReturnBadArgument;
   copy=IOMalloc(bytes);if(!copy)return kIOReturnNoMemory;
   IOReturn prepared=a->structureInputDescriptor->prepare(kIODirectionOut);
   if(prepared!=kIOReturnSuccess){IOFree(copy,bytes);return prepared;}
   IOByteCount copied=a->structureInputDescriptor->readBytes(0,copy,bytes);
   IOReturn completed=a->structureInputDescriptor->complete(kIODirectionOut);
   if(copied!=bytes||completed!=kIOReturnSuccess){IOFree(copy,bytes);return completed!=kIOReturnSuccess?completed:kIOReturnBadArgument;}
   carrier=copy;
  }else if(a->structureInputSize!=bytes)return kIOReturnBadArgument;
  struct niw_request q;IOReturn result=kIOReturnBadArgument;
  if(!niw_decode(selector,carrier,bytes,&q)){
   result=kIOReturnNotReady;
   if(selector==0&&q.request==43&&q.value==0&&q.payload_bytes==0){
    static const char identity[]="Nord2 private lifecycle shell (unregistered)";size_t used=0;
    if(!niwb_driver_version(carrier,bytes,identity,sizeof identity,a->structureOutput,a->structureOutputSize,&used)){
     a->structureOutputSize=(uint32_t)used;result=kIOReturnSuccess;
    }else result=kIOReturnBadArgument;
   }
  }
  if(copy)IOFree(copy,bytes);
  /* No CARD/security inventory requirement is invented: all other requests
   * simply lack an installed backend. SET unsupported by GET-only codec. */
  return result;
 }
 if(a->structureInputDescriptor||a->structureInputSize||a->scalarInputCount||a->scalarOutputCount)return kIOReturnBadArgument;
 if(selector==1||selector==5){
  if(a->asyncReferenceCount||a->asyncWakePort)return kIOReturnBadArgument;
  struct niwb_dispatch_view view={NIWB_OPEN,0,0};size_t used=0;
  if(niwb_selector(&view,selector,0,a->structureOutputSize,0,0,0,(uint8_t*)a->structureOutput,&used))return kIOReturnBadArgument;
  a->structureOutputSize=(uint32_t)used;return kIOReturnSuccess; /* honest unsupported0 */
 }
 if(a->structureOutputSize)return kIOReturnBadArgument;
 if(selector==2){
  if(phase!=Allocated)return kIOReturnNotReady;
  if(a->asyncReferenceCount!=8||!a->asyncReference||!a->asyncWakePort)return kIOReturnBadArgument;
  for(unsigned i=0;i<8;i++)reference[i]=a->asyncReference[i];ownsReference=true;*adopted=true;phase=Armed;return kIOReturnSuccess;
 }
 if(a->asyncReferenceCount||a->asyncWakePort)return kIOReturnBadArgument;
 if(selector==3){
  if(phase!=Open||generation==UINT64_MAX)return kIOReturnBusy;
  struct niwe_view empty={0,0,4096};struct niwe_space space;
  if(niwe_plan(&empty,4,0,&space)!=1)return kIOReturnBadArgument; /* bounds only, no event publication */
  IOBufferMemoryDescriptor*d=IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,kIOMemoryKernelUserShared,4096,4096);
  if(!d)return kIOReturnNoMemory;
  IOBufferMemoryDescriptor*s=IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,kIOMemoryKernelUserShared,4096,4096);
  if(!s){d->release();return kIOReturnNoMemory;}
  bzero(d->getBytesNoCopy(),4096);bzero(s->getBytesNoCopy(),4096);data=d;state=s;phase=Allocated;generation++;return kIOReturnSuccess;
 }
 if(selector==4){discardLocked();phase=Open;return kIOReturnSuccess;}
 return kIOReturnUnsupported;
}
IOReturn NiwlLifecycleClient::externalMethod(uint32_t selector,IOExternalMethodArguments*a,IOExternalMethodDispatch*d,OSObject*t,void*r){
 /* XNU passes an owned wake-port send reference on async calls. Balance every
  * rejected/replayed call too, not only the accepted stored registration. */
 OSAsyncReference64 incoming={0};bool incomingRight=a&&a->asyncReference&&a->asyncReferenceCount;
 if(incomingRight)incoming[0]=a->asyncReference[0];bool adopted=false;IOReturn result=kIOReturnNotReady;
 if(gate){IOLockLock(gate);result=(d||t||r)?kIOReturnBadArgument:dispatchLocked(selector,a,&adopted);IOLockUnlock(gate);}
 if(incomingRight&&!adopted)IOUserClient::releaseAsyncReference64(incoming);return result;
}
IOReturn NiwlLifecycleClient::clientMemoryForType(UInt32 type,IOOptionBits*options,IOMemoryDescriptor**memory){
 if(!gate||!options||!memory)return kIOReturnBadArgument;IOLockLock(gate);
 IOReturn result=kIOReturnNotReady;
 if(phase==Closed)result=kIOReturnNotOpen;
 else if(current_task()!=owner)result=kIOReturnNotPrivileged;
 else if(type!=1&&type!=2)result=kIOReturnBadArgument;
 else if(phase==Allocated||phase==Armed){IOMemoryDescriptor*m=type==1?data:state;m->retain();*memory=m;*options=type==1?kIOMapReadOnly:0;result=kIOReturnSuccess;}
 IOLockUnlock(gate);return result;
}
IOReturn NiwlLifecycleClient::clientClose(void){closeOwned();terminate();return kIOReturnSuccess;}
IOReturn NiwlLifecycleClient::clientDied(void){closeOwned();terminate();return kIOReturnSuccess;}
void NiwlLifecycleClient::stop(IOService*p){closeOwned();IOUserClient::stop(p);}
void NiwlLifecycleClient::free(void){closeOwned();if(owner){task_deallocate(owner);owner=TASK_NULL;}if(gate){IOLockFree(gate);gate=nullptr;}IOUserClient::free();}
class NiwlLifecycleService : public IOService {
 OSDeclareDefaultStructors(NiwlLifecycleService)
public:
 IOReturn newUserClient(task_t,void*,UInt32,OSDictionary*,IOUserClient**) override;
};
OSDefineMetaClassAndStructors(NiwlLifecycleService,IOService)
IOReturn NiwlLifecycleService::newUserClient(task_t task,void*security,UInt32 type,OSDictionary*props,IOUserClient**handler){
 if(!handler||isInactive())return kIOReturnNotReady;
 NiwlLifecycleClient*c=new NiwlLifecycleClient;
 if(!c)return kIOReturnNoMemory;
 if(!c->initWithTask(task,security,type,props)){c->release();return kIOReturnNotPrivileged;}
 if(!c->attach(this)){c->release();return kIOReturnNotReady;}
 if(!c->start(this)){c->detach(this);c->release();return kIOReturnNotReady;}
 *handler=c;return kIOReturnSuccess;
}
