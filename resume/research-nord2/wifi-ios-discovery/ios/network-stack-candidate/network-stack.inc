/* Original bounded adapter for an interface already attached by the BSD KPI.
 * Contract constants are corroborated by Apple's published IONetworkStack and
 * the selected configd consumer. This is not a copy of either implementation.
 * No ifnet is created, renamed, brought up or given a working data path here. */
static unsigned long long niws_ms(void){
#ifdef NIWI_REGISTRY_HOST_TEST
 return 0;
#else
 uint64_t now=0,ns=0;clock_get_uptime(&now);absolutetime_to_nanoseconds(now,&ns);return ns/1000000u;
#endif
}
class IONetworkStackUserClient;
class IONetworkStack : public IOService {
 OSDeclareDefaultStructors(IONetworkStack)
 IOLock *gate=nullptr;
 IOService *ownedInterface=nullptr;
 ifnet_t ownedNet=nullptr;
 uint64_t entryID=0;
 uint32_t unit=0;
 char bsdName[16]={};
 bool active=false;unsigned openLogs=0,propertyLogs=0;
 bool liveLocked(){
  if(!active||!ownedInterface||ownedInterface->isInactive()||!ownedNet)return false;
  ifnet_t found=nullptr;
  errno_t e=ifnet_find_by_name(bsdName,&found);
  bool same=!e&&found==ownedNet&&ifnet_unit(found)==unit;
  if(found)ifnet_release(found);
  return same;
 }
public:
 bool initOwned(IOService *interface,ifnet_t net){
  if(!interface||!net||!IOService::init())return false;
  gate=IOLockAlloc();if(!gate)return false;
  if(interface->isInactive()||strcmp(ifnet_name(net),"en")||ifnet_unit(net)>=32)return false;
  unit=ifnet_unit(net);snprintf(bsdName,sizeof bsdName,"en%u",unit);
  ifnet_t found=nullptr;errno_t e=ifnet_find_by_name(bsdName,&found);
  if(e||found!=net){if(found)ifnet_release(found);return false;}
  ownedNet=found;ownedInterface=interface;interface->retain();entryID=interface->getRegistryEntryID();
  return entryID!=0;
 }
 bool start(IOService *provider) override {
  if(!gate||!ownedInterface||!ownedNet||!IOService::start(provider))return false;
  if(!setProperty("IOUserClientClass","IONetworkStackUserClient")){IOService::stop(provider);return false;}
  setName("IONetworkStack");active=true;return true;
 }
 void deactivate(){if(gate){IOLockLock(gate);active=false;IOLockUnlock(gate);}}
 void stop(IOService *provider) override {deactivate();IOService::stop(provider);}
 void free(void) override {
  if(ownedInterface){ownedInterface->release();ownedInterface=nullptr;}
  if(ownedNet){ifnet_release(ownedNet);ownedNet=nullptr;}
  if(gate){IOLockFree(gate);gate=nullptr;}
  IOService::free();
 }
 IOReturn setProperties(OSObject *properties) override {
  if(IOUserClient::clientHasPrivilege(current_task(),kIOClientPrivilegeAdministrator)!=kIOReturnSuccess)return kIOReturnNotPrivileged;
  OSDictionary *dict=OSDynamicCast(OSDictionary,properties);
  if(!dict)return kIOReturnBadArgument;
  OSNumber *command=OSDynamicCast(OSNumber,dict->getObject("IONetworkStackUserCommand"));
  if(!command)return kIOReturnBadArgument;
  uint64_t cmd=command->unsigned64BitValue(),target=0,requested=0;
  if(cmd>2)return kIOReturnUnsupported;
  if(cmd==2){
   /* Global operation, not an individual target. Owned pending set is empty:
    * the sole owned interface was attached by the BSD KPI before publication. */
   if(dict->getObject("IORegistryEntryID")||dict->getObject("IOInterfaceUnit"))return kIOReturnBadArgument;
  }else{
   OSNumber *requestedUnit=OSDynamicCast(OSNumber,dict->getObject("IOInterfaceUnit"));
   OSData *id=OSDynamicCast(OSData,dict->getObject("IORegistryEntryID"));
   /* Selected caller supplies all three. No path fallback/numeric truncation. */
   if(!requestedUnit||!id||id->getLength()!=sizeof(uint64_t))return kIOReturnBadArgument;
   requested=requestedUnit->unsigned64BitValue();if(requested>UINT32_MAX)return kIOReturnBadArgument;
   memcpy(&target,id->getBytesNoCopy(),sizeof target);
  }
  if(!gate)return kIOReturnNotReady;
  IOLockLock(gate);IOReturn result=kIOReturnNotReady;bool live=liveLocked();
  if(live){
   if(cmd==2)result=kIOReturnSuccess;
   else if(target!=entryID)result=kIOReturnNoDevice;
   /* Already attached interfaces cannot be re-reserved by Apple's state
    * machine. Preserve NoSpace on repeat; do not claim rename/new attach. */
   else result=kIOReturnNoSpace;
  }
  if(propertyLogs<16){propertyLogs++;printf("NORD2 WIFI IOS STACK SET: command=%u unit=%u live=%u status=%#x t_ms=%llu\n",(unsigned)cmd,(unsigned)requested,(unsigned)live,(unsigned)result,niws_ms());}
  IOLockUnlock(gate);return result;
 }
 IOReturn newUserClient(task_t,void*,UInt32,OSDictionary*,IOUserClient**) override;
 void opened(){IOLockLock(gate);if(openLogs<8){openLogs++;printf("NORD2 WIFI IOS STACK OPEN: type=0 unit=%u ifstate=1 t_ms=%llu\n",unit,niws_ms());}IOLockUnlock(gate);}
 bool ready(){if(!gate)return false;IOLockLock(gate);bool r=liveLocked();IOLockUnlock(gate);return r;}
};
OSDefineMetaClassAndStructors(IONetworkStack,IOService)

class IONetworkStackUserClient : public IOUserClient {
 OSDeclareDefaultStructors(IONetworkStackUserClient)
 IOLock *gate=nullptr;
 task_t owner=TASK_NULL;
 IONetworkStack *stack=nullptr;
 void closeOwned(){
  if(!gate)return;IOLockLock(gate);IONetworkStack *old=stack;stack=nullptr;
  task_t task=owner;owner=TASK_NULL;IOLockUnlock(gate);
  if(old){old->close(this);old->release();}if(task)task_deallocate(task);
 }
public:
 bool initWithTask(task_t task,void *security,UInt32 type,OSDictionary *properties) override {
  if(!task||current_task()!=task||type||IOUserClient::clientHasPrivilege(security,kIOClientPrivilegeAdministrator)!=kIOReturnSuccess)return false;
  if(!IOUserClient::initWithTask(task,security,type,properties))return false;
  gate=IOLockAlloc();if(!gate)return false;task_reference(task);owner=task;return true;
 }
 bool start(IOService *provider) override {
  IONetworkStack *candidate=OSDynamicCast(IONetworkStack,provider);
  if(!candidate||!candidate->ready()||!IOUserClient::start(provider))return false;
  if(!candidate->open(this)){IOUserClient::stop(provider);return false;}
  candidate->retain();stack=candidate;return true;
 }
 IOReturn setProperties(OSObject *properties) override {
  if(!gate)return kIOReturnNotOpen;
  IOLockLock(gate);IOReturn result=kIOReturnNotOpen;
  if(stack){
   if(current_task()!=owner||IOUserClient::clientHasPrivilege(current_task(),kIOClientPrivilegeAdministrator)!=kIOReturnSuccess)result=kIOReturnNotPrivileged;
   else result=stack->setProperties(properties);
  }
  IOLockUnlock(gate);return result;
 }
 IOReturn clientClose(void) override {closeOwned();if(!isInactive())terminate();return kIOReturnSuccess;}
 IOReturn clientDied(void) override {return clientClose();}
 void stop(IOService *provider) override {closeOwned();IOUserClient::stop(provider);}
 void free(void) override {closeOwned();if(gate){IOLockFree(gate);gate=nullptr;}IOUserClient::free();}
};
OSDefineMetaClassAndStructors(IONetworkStackUserClient,IOUserClient)
IOReturn IONetworkStack::newUserClient(task_t task,void *security,UInt32 type,OSDictionary *properties,IOUserClient **out){
 if(!out)return kIOReturnBadArgument;*out=nullptr;
 if(type)return kIOReturnUnsupported;
 if(!task||current_task()!=task||IOUserClient::clientHasPrivilege(security,kIOClientPrivilegeAdministrator)!=kIOReturnSuccess)return kIOReturnNotPrivileged;
 if(!ready())return kIOReturnNotReady;
 auto *client=new IONetworkStackUserClient;
 if(!client)return kIOReturnNoMemory;
 if(!client->initWithTask(task,security,type,properties)){client->release();return kIOReturnNoResources;}
 if(!client->attach(this)){client->release();return kIOReturnNoResources;}
 if(!client->start(this)){client->detach(this);client->release();return kIOReturnNotReady;}
 *out=client;opened();return kIOReturnSuccess;
}
