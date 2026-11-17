/* Private, unregistered, uninstantiated refusal-only skeleton. This is NOT
 * IO80211APIUserClient and cannot satisfy wifid's class entitlement/matching.
 * No selector implements success, no memory/event/packet/backend is exposed. */
#include <IOKit/IOUserClient.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSBoolean.h>
#include <kern/task.h>
class Nord2WiFiControlCandidate : public IOUserClient {
 OSDeclareDefaultStructors(Nord2WiFiControlCandidate)
 task_t owner;
 IOLock *lock;
 bool closed;
public:
 bool initWithTask(task_t task,void *security,UInt32 type,OSDictionary *props) override;
 IOReturn externalMethod(uint32_t,IOExternalMethodArguments *,IOExternalMethodDispatch *,OSObject *,void *) override;
 IOReturn clientClose(void) override;
 IOReturn clientMemoryForType(UInt32,IOOptionBits *,IOMemoryDescriptor **) override;
 void free(void) override;
};
OSDefineMetaClassAndStructors(Nord2WiFiControlCandidate,IOUserClient)
bool Nord2WiFiControlCandidate::initWithTask(task_t task,void *security,UInt32 type,OSDictionary *props){
 owner=TASK_NULL;lock=nullptr;closed=true;
 if(!task||type!=0)return false;
 OSObject *entitlement=IOUserClient::copyClientEntitlement(task,"com.apple.wifi.manager-access");
 OSBoolean *allowed=OSDynamicCast(OSBoolean,entitlement);
 bool authorized=allowed&&allowed->isTrue();
 if(entitlement)entitlement->release();
 if(!authorized)return false;
 if(!IOUserClient::initWithTask(task,security,type,props))return false;
 lock=IOLockAlloc();if(!lock)return false;
 task_reference(task);owner=task;closed=false;return true;
}
IOReturn Nord2WiFiControlCandidate::externalMethod(uint32_t,IOExternalMethodArguments *,IOExternalMethodDispatch *,OSObject *,void *){
 if(!lock)return kIOReturnNotReady;
 IOLockLock(lock);
 IOReturn result=closed?kIOReturnNotOpen:(current_task()!=owner?kIOReturnNotPrivileged:kIOReturnUnsupported);
 IOLockUnlock(lock);return result;
}
IOReturn Nord2WiFiControlCandidate::clientMemoryForType(UInt32,IOOptionBits *,IOMemoryDescriptor **){return kIOReturnUnsupported;}
IOReturn Nord2WiFiControlCandidate::clientClose(void){
 if(lock){IOLockLock(lock);closed=true;IOLockUnlock(lock);}
 terminate();return kIOReturnSuccess;
}
void Nord2WiFiControlCandidate::free(void){
 if(owner){task_deallocate(owner);owner=TASK_NULL;}
 if(lock){IOLockFree(lock);lock=nullptr;}
 IOUserClient::free();
}
