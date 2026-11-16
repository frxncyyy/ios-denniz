#include <arm64/sweet_early.h>
#if defined(NORD2)
/* 0451: user client of the 802.1X supplicant (eapolclient) on the IO80211 services of wifi_ios_registry.cpo, as a NEW
 * object. eapolclient has com.apple.wlan.authentication but not com.apple.wifi.manager-access, so the CI client
 * (wifi_ios_client.cpo, not rebuilt) refuses it; without an Apple80211 handle eapolclient treats en0 as wired Ethernet
 * and never hands the EAP key to the driver.
 *  - The CI factory is renamed in the archive copy (niwl_create_client -> niwc_create_client, symbol name only); the
 *    registry's unchanged call lands in niwl_create_client below, which forwards every manager client (wifid) to the
 *    CI factory as before and gives this small client only to a task with the supplicant entitlement.
 *  - It speaks the same carriers as the CI client (IO80211.framework RawGet/RawSet, selectors 0 and 6; transport shape
 *    selectors 1 and 5 answered by the same niwb_selector): GET 43/44 (bind, names), 1 SSID, 9 BSSID, 2 AUTH_TYPE,
 *    4 CHANNEL, 13 STATE of the 802.1X association (wifi_8021x_glue.c); SET 3 APPLE80211_IOC_CIPHER_KEY (struct
 *    apple80211_key, 148 bytes: version 1 at 0, key length at 4, cipher at 8, key at 16; cipher 9 = MSK 64 bytes,
 *    6 = PMK 32) and SET 89 (clear a PMK cache this driver does not keep). No scan, no event pipe, no join/leave/power.
 * Key bytes are never logged. */
#include <IOKit/IOUserClient.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOMessage.h>
#include <libkern/c++/OSBoolean.h>
#include <libkern/c++/OSString.h>
#include <kern/task.h>
#include <kern/clock.h>
extern "C" {
#include "../../../bsd/dev/wifi_ios_binding.h"
#include "../../../bsd/dev/wifi_ios_control.h"
#include "../../../bsd/dev/wifi_ios_policy.h"
#include "../../../bsd/dev/wifi_8021x_glue.h"
IOReturn niwc_create_client(IOService*,task_t,void*,UInt32,OSDictionary*,IOUserClient**); /* the CI factory, renamed */
IOReturn niwl_create_client(IOService*,task_t,void*,UInt32,OSDictionary*,IOUserClient**);
extern void (*n8x_port_open_hook)(void);
}
static uint32_t n8s_u32(const uint8_t*p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static unsigned long long n8s_ms(void){uint64_t now=0,ns=0;clock_get_uptime(&now);absolutetime_to_nanoseconds(now,&ns);return ns/1000000u;}
static void n8s_free(void*p,size_t n){if(p){volatile uint8_t*q=(volatile uint8_t*)p;for(size_t i=0;i<n;i++)q[i]=0;IOFree(p,n);}}
static bool n8s_entitled(task_t task,const char*name){
 OSObject*entitlement=IOUserClient::copyClientEntitlement(task,name);OSBoolean*allowed=OSDynamicCast(OSBoolean,entitlement);
 bool yes=allowed&&allowed->isTrue();if(entitlement)entitlement->release();return yes;
}
/* 0453: eapolclient's "4-way handshake complete" (it then asks for the DHCP renewal: "force renew"). Verified in its code:
 * IOServiceAddInterestNotification(kIOGeneralInterest) on the service two levels above the registry entry with the BSD
 * name, handler taken for messageType 0xE0000130 = kIOMessageServicePropertyChange. Sent from the 802.1X thread call when
 * the keys are in place, to the clients of the service the supplicant opened and of its two ancestors. */
static IOService*n8s_service;
static void n8s_handshake_complete(void){
 unsigned sent=0;IOService*s=n8s_service;
 for(unsigned level=0;s&&level<3;level++,s=s->getProvider()){(void)s->messageClients(kIOMessageServicePropertyChange);sent++;}
 printf("NORD2 WIFI 8021X: handshake complete message sent services=%u\n",sent);
}
class Nord2WifiSupplicantClient : public IOUserClient {
 OSDeclareDefaultStructors(Nord2WifiSupplicantClient)
 task_t owner;IOLock*gate;unsigned logs;char interfaceName[16];
 IOReturn carrierLocked(uint32_t selector,const uint8_t*carrier,size_t bytes,void*out,size_t capacity,size_t*used);
 IOReturn dispatchLocked(uint32_t selector,IOExternalMethodArguments*a);
public:
 void bindInterface(const char*n){strlcpy(interfaceName,n,sizeof interfaceName);}
 bool initWithTask(task_t,void*,UInt32,OSDictionary*) override;
 IOReturn externalMethod(uint32_t,IOExternalMethodArguments*,IOExternalMethodDispatch*,OSObject*,void*) override;
 IOReturn clientClose(void) override;
 IOReturn clientDied(void) override;
 void free(void) override;
};
OSDefineMetaClassAndStructors(Nord2WifiSupplicantClient,IOUserClient)
bool Nord2WifiSupplicantClient::initWithTask(task_t task,void*security,UInt32 type,OSDictionary*props){
 owner=TASK_NULL;gate=nullptr;logs=0;bzero(interfaceName,sizeof interfaceName);
 if(!task||current_task()!=task||type||!n8s_entitled(task,"com.apple.wlan.authentication"))return false;
 if(!IOUserClient::initWithTask(task,security,type,props))return false;
 gate=IOLockAlloc();if(!gate)return false;
 task_reference(task);owner=task;return true;
}
/* One complete carrier already copied into kernel memory. */
IOReturn Nord2WifiSupplicantClient::carrierLocked(uint32_t selector,const uint8_t*carrier,size_t bytes,void*out,size_t capacity,size_t*used){
 uint32_t set=0;struct niw_request q={};IOReturn result=kIOReturnBadArgument;unsigned kind=0,request=0,extent=0;
 if(niwi_scan_envelope(selector,carrier,bytes,interfaceName)==0){kind=3;result=kIOReturnUnsupported;}
 else if(!niwi_set_shape(selector,carrier,bytes,interfaceName,&set)){
  size_t tail=selector==0?15368:245768;const uint8_t*key=carrier+8;uint32_t value=n8s_u32(carrier+tail+20);
  kind=2;request=set;extent=n8s_u32(carrier+4);
  if(value)result=kIOReturnBadArgument;
  else if(set==89)result=extent?kIOReturnUnsupported:kIOReturnSuccess;
  else if(set!=3)result=kIOReturnUnsupported;
  else if(extent!=148||n8s_u32(key)!=1)result=kIOReturnBadArgument;
  else{
   uint32_t cipher=n8s_u32(key+8),length=n8s_u32(key+4);
   if((cipher==9&&length==64)||(cipher==6&&length==32)){int x=nord2_wifi_8021x_key(key+16,length);result=!x?kIOReturnSuccess:x<0?kIOReturnBadArgument:kIOReturnNotReady;}
   else if(cipher==9&&!length)result=kIOReturnSuccess;
   else result=kIOReturnUnsupported;
   if(logs<48){logs++;printf("NORD2 WIFI 8021X: supplicant key set cipher=%u key_bytes=%u status=%#x t_ms=%llu\n",cipher,length,(unsigned)result,n8s_ms());}
  }
 }else if(!niw_decode(selector,carrier,bytes,&q)){
  kind=1;request=q.request;extent=q.payload_bytes;
  if(memcmp(q.interface_name,interfaceName,16)||q.value)result=kIOReturnBadArgument;
  else if(selector==0&&q.request==43&&q.payload_bytes==0){
   static const char identity[]="Nord2 MT6893 passive scan controller";size_t n=0;
   if(!niwb_driver_version(carrier,bytes,identity,sizeof identity,out,capacity,&n)){*used=n;result=kIOReturnSuccess;}
  }else if(q.request==43||q.request==44){
   static const char driver[]="Nord2 MT6893 passive scan controller";static const char hardware[]="Nord2 MT6893 Wi-Fi";
   if(q.payload_bytes==256&&capacity>=256&&out){bzero(out,256);if(q.request==43)memcpy(out,driver,sizeof driver);else memcpy(out,hardware,sizeof hardware);*used=256;result=kIOReturnSuccess;}
  }else{
   size_t n=0;int x=out?nord2_wifi_8021x_get(q.request,q.payload_bytes,out,capacity,&n):-1;
   if(!x){*used=n;result=kIOReturnSuccess;}else result=x==1?(IOReturn)6:x==-2?kIOReturnUnsupported:kIOReturnBadArgument; /* 6 = ENXIO: no association, as the CI client answers */
  }
 }
 if(logs<48){logs++;printf("NORD2 WIFI 8021X: supplicant request selector=%u kind=%u request=%u bytes=%u status=%#x\n",selector,kind,request,extent,(unsigned)result);}
 return result;
}
IOReturn Nord2WifiSupplicantClient::dispatchLocked(uint32_t selector,IOExternalMethodArguments*a){
 if(!owner)return kIOReturnNotOpen;
 if(current_task()!=owner)return kIOReturnNotPrivileged;
 if(a->version!=kIOExternalMethodArgumentsCurrentVersion||a->selector!=selector||a->scalarInputCount||a->scalarOutputCount||a->asyncReferenceCount||a->asyncWakePort)return kIOReturnBadArgument;
 if(a->structureOutputSize&&!a->structureOutput)return kIOReturnBadArgument;
 if(selector==1||selector==5){
  if(a->structureInputDescriptor||a->structureInputSize||a->structureOutputDescriptor)return kIOReturnBadArgument;
  struct niwb_dispatch_view view={NIWB_OPEN,NIWB_CONTROL_ALL,NIWB_PIPE_ALL};size_t n=0;
  if(niwb_selector(&view,selector,0,a->structureOutputSize,0,0,0,(uint8_t*)a->structureOutput,&n))return kIOReturnBadArgument;
  a->structureOutputSize=(uint32_t)n;return kIOReturnSuccess;
 }
 if(selector!=0&&selector!=6)return kIOReturnUnsupported; /* no event pipe for the supplicant */
 size_t bytes=selector==0?NIW_SMALL_BYTES:NIW_LARGE_BYTES;const uint8_t*carrier=(const uint8_t*)a->structureInput;void*copy=nullptr;
 if(a->structureInputDescriptor){
  if(a->structureInputSize||a->structureInputDescriptor->getLength()!=bytes)return kIOReturnBadArgument;
  copy=IOMalloc(bytes);if(!copy)return kIOReturnNoMemory;
  IOReturn prepared=a->structureInputDescriptor->prepare(kIODirectionOut);
  if(prepared!=kIOReturnSuccess){n8s_free(copy,bytes);return prepared;}
  IOByteCount copied=a->structureInputDescriptor->readBytes(0,copy,bytes);
  IOReturn completed=a->structureInputDescriptor->complete(kIODirectionOut);
  if(copied!=bytes||completed!=kIOReturnSuccess){n8s_free(copy,bytes);return completed!=kIOReturnSuccess?completed:kIOReturnBadArgument;}
  carrier=(const uint8_t*)copy;
 }else if(a->structureInputSize!=bytes||!carrier)return kIOReturnBadArgument;
 IOReturn result;size_t n=0;
 if(a->structureOutputDescriptor){
  IOMemoryDescriptor*md=a->structureOutputDescriptor;size_t capacity=a->structureOutputDescriptorSize;
  if(a->structureOutputSize||!capacity||capacity>NIW_LARGE_PAYLOAD||md->getLength()!=capacity)result=kIOReturnBadArgument;
  else{
   void*buffer=IOMalloc(capacity);
   if(!buffer)result=kIOReturnNoMemory;
   else{
    bzero(buffer,capacity);result=md->prepare(kIODirectionIn);
    if(result==kIOReturnSuccess){
     result=carrierLocked(selector,carrier,bytes,buffer,capacity,&n);
     if(result==kIOReturnSuccess&&(n>capacity||md->writeBytes(0,buffer,n)!=n))result=kIOReturnBadArgument;
     IOReturn completed=md->complete(kIODirectionIn);if(result==kIOReturnSuccess&&completed!=kIOReturnSuccess)result=completed;
     if(result==kIOReturnSuccess)a->structureOutputDescriptorSize=(uint32_t)n;
    }
    IOFree(buffer,capacity);
   }
  }
 }else{
  result=carrierLocked(selector,carrier,bytes,a->structureOutput,a->structureOutputSize,&n);
  if(result==kIOReturnSuccess)a->structureOutputSize=(uint32_t)n;
 }
 if(copy)n8s_free(copy,bytes);
 return result;
}
IOReturn Nord2WifiSupplicantClient::externalMethod(uint32_t selector,IOExternalMethodArguments*a,IOExternalMethodDispatch*d,OSObject*t,void*r){
 /* XNU passes an owned wake-port send reference on async calls; this client adopts none. */
 OSAsyncReference64 incoming={0};bool incomingRight=a&&a->asyncReference&&a->asyncReferenceCount;if(incomingRight)incoming[0]=a->asyncReference[0];
 IOReturn result=kIOReturnBadArgument;
 if(gate&&a&&!d&&!t&&!r){IOLockLock(gate);result=dispatchLocked(selector,a);IOLockUnlock(gate);}
 if(incomingRight)IOUserClient::releaseAsyncReference64(incoming);
 return result;
}
IOReturn Nord2WifiSupplicantClient::clientClose(void){terminate();return kIOReturnSuccess;}
IOReturn Nord2WifiSupplicantClient::clientDied(void){terminate();return kIOReturnSuccess;}
void Nord2WifiSupplicantClient::free(void){if(owner){task_deallocate(owner);owner=TASK_NULL;}if(gate){IOLockFree(gate);gate=nullptr;}IOUserClient::free();}
/* The registry's factory call. Manager clients get the CI client exactly as before. */
extern "C" IOReturn niwl_create_client(IOService*provider,task_t task,void*security,UInt32 type,OSDictionary*props,IOUserClient**handler){
 bool supplicant=(niwj_8021x&1u)&&provider&&handler&&task&&current_task()==task&&!type&&!n8s_entitled(task,"com.apple.wifi.manager-access")&&n8s_entitled(task,"com.apple.wlan.authentication");
 if(!supplicant)return niwc_create_client(provider,task,security,type,props,handler);
 if(provider->isInactive())return kIOReturnNotReady;
 OSString*name=OSDynamicCast(OSString,provider->getProperty("IOInterfaceName"));if(!name||name->getLength()>=16)return kIOReturnNotReady;
 Nord2WifiSupplicantClient*c=new Nord2WifiSupplicantClient;if(!c)return kIOReturnNoMemory;
 if(!c->initWithTask(task,security,type,props)){c->release();return kIOReturnNotPrivileged;}
 c->bindInterface(name->getCStringNoCopy());
 if(!c->attach(provider)){c->release();return kIOReturnNotReady;}
 if(!c->start(provider)){c->detach(provider);c->release();return kIOReturnNotReady;}
 if(!n8s_service){provider->retain();n8s_service=provider;}n8x_port_open_hook=n8s_handshake_complete;
 *handler=c;printf("NORD2 WIFI 8021X: supplicant client opened t_ms=%llu\n",n8s_ms());return kIOReturnSuccess;
}

#endif /* NORD2 */
