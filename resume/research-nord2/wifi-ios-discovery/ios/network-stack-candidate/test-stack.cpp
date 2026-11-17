#define NIWI_REGISTRY_HOST_TEST 1
#include "test-stack-xnu.h"
#include "network-stack.inc"
static unsigned checks;
#define CHECK(v) do{checks++;assert(v);}while(0)
static OSDictionary*request(uint64_t id,uint64_t cmd=0,uint64_t unit=0,size_t bytes=8){uint8_t data[16]={};assert(bytes<=sizeof data);memcpy(data,&id,sizeof id);auto*d=new OSDictionary;OSObject*p=new OSData(data,bytes);d->setObject("IORegistryEntryID",p);p->release();p=new OSNumber(cmd);d->setObject("IONetworkStackUserCommand",p);p->release();p=new OSNumber(unit);d->setObject("IOInterfaceUnit",p);p->release();return d;}
static void resetFaults(){failLockAt=failPropertyAt=failAttachAt=failStartAt=0;lockAttempts=propertyCalls=attachCalls=startCalls=0;}
static IONetworkStack*bound(IOService*i,ifnet_t n,IOService*p){auto*s=new IONetworkStack;CHECK(s->initOwned(i,n));CHECK(s->attach(p));CHECK(s->start(p));return s;}
int main(){task admin,other,unpriv;unpriv.admin=false;caller=&admin;
 {IOService interface,resources;ifnet net;table["en0"]=&net;auto*s=bound(&interface,&net,&resources);CHECK(net.refs==2);
  IOUserClient*c=nullptr;CHECK(s->newUserClient(&admin,&admin,0,nullptr,&c)==0&&c);CHECK(admin.refs==2);
  auto*d=request(interface.getRegistryEntryID());CHECK(c->setProperties(d)==kIOReturnNoSpace);CHECK(net.unit==0&&net.attached&&net.refs==2);
  for(uint64_t cmd:{0,1})for(uint64_t unit:std::vector<uint64_t>{0,1,31,UINT32_MAX}){auto*q=request(interface.getRegistryEntryID(),cmd,unit);CHECK(c->setProperties(q)==kIOReturnNoSpace);q->release();}
  auto*all=new OSDictionary;auto*allCommand=new OSNumber(2);all->setObject("IONetworkStackUserCommand",allCommand);allCommand->release();CHECK(c->setProperties(all)==0);auto*badAll=request(interface.getRegistryEntryID(),2);CHECK(c->setProperties(badAll)==kIOReturnBadArgument);badAll->release();
  auto*q=request(interface.getRegistryEntryID()+1);CHECK(c->setProperties(q)==kIOReturnNoDevice);q->release();
  for(uint64_t cmd:std::vector<uint64_t>{3,UINT64_MAX}){q=request(interface.getRegistryEntryID(),cmd);CHECK(c->setProperties(q)==kIOReturnUnsupported);q->release();}
  q=request(interface.getRegistryEntryID(),0,UINT64_MAX);CHECK(c->setProperties(q)==kIOReturnBadArgument);q->release();
  for(size_t bytes:{0,1,7,9,16}){q=request(interface.getRegistryEntryID(),0,0,bytes);CHECK(c->setProperties(q)==kIOReturnBadArgument);q->release();}
  for(const char*key:{"IONetworkStackUserCommand","IORegistryEntryID","IOInterfaceUnit"}){q=request(interface.getRegistryEntryID());q->removeObject(key);CHECK(c->setProperties(q)==kIOReturnBadArgument);OSObject*wrong=new OSString("bad");q->setObject(key,wrong);wrong->release();CHECK(c->setProperties(q)==kIOReturnBadArgument);q->release();}
  CHECK(c->setProperties(nullptr)==kIOReturnBadArgument);caller=&other;CHECK(c->setProperties(d)==kIOReturnNotPrivileged);caller=&admin;admin.admin=false;CHECK(c->setProperties(d)==kIOReturnNotPrivileged);admin.admin=true;
  IOUserClient*out=(IOUserClient*)1;CHECK(s->newUserClient(&admin,&admin,1,nullptr,&out)==kIOReturnUnsupported&&!out);caller=&unpriv;CHECK(s->newUserClient(&unpriv,&unpriv,0,nullptr,&out)==kIOReturnNotPrivileged&&!out);caller=&admin;
  CHECK(s->newUserClient(&other,&other,0,nullptr,&out)==kIOReturnNotPrivileged&&!out);CHECK(s->newUserClient(&admin,&admin,0,nullptr,nullptr)==kIOReturnBadArgument);
  net.attached=false;CHECK(c->setProperties(d)==kIOReturnNotReady);net.attached=true;ifnet replacement;table["en0"]=&replacement;CHECK(c->setProperties(d)==kIOReturnNotReady);CHECK(replacement.refs==1);table["en0"]=&net;
  s->stop(&resources);CHECK(c->setProperties(all)==kIOReturnNotReady);all->release();CHECK(c->setProperties(d)==kIOReturnNotReady);CHECK(c->clientClose()==0);CHECK(c->clientDied()==0);CHECK(c->setProperties(d)==kIOReturnNotOpen);CHECK(admin.refs==1);c->release();s->release();d->release();CHECK(net.refs==1);table.clear();
 }
 CHECK(objects==0&&locks==0);
 /* Client allocation/start/attach/property failures must not retain task/net. */
 for(unsigned fault=0;fault<4;fault++){
  resetFaults();IOService interface,resources;ifnet net;table["en0"]=&net;auto*s=bound(&interface,&net,&resources);
  if(fault==0)failLockAt=lockAttempts+1;if(fault==1)failAttachAt=attachCalls+1;if(fault==2)failStartAt=startCalls+1;
  if(fault==3){IOUserClient*first=nullptr;CHECK(s->newUserClient(&admin,&admin,0,nullptr,&first)==0);IOUserClient*second=nullptr;CHECK(s->newUserClient(&admin,&admin,0,nullptr,&second)!=0&&!second);first->clientClose();first->release();}
  else{IOUserClient*c=(IOUserClient*)1;CHECK(s->newUserClient(&admin,&admin,0,nullptr,&c)!=0&&!c);}
  CHECK(admin.refs==1);resetFaults();s->stop(&resources);s->release();CHECK(net.refs==1);table.clear();
 }
 CHECK(objects==0&&locks==0);
 for(unsigned fault=0;fault<5;fault++){
  resetFaults();IOService interface,resources;ifnet net;table["en0"]=&net;auto*s=new IONetworkStack;
  if(fault==0)failLockAt=1;if(fault==1)table.clear();if(fault==2)net.prefix="bad";
  bool ok=s->initOwned(&interface,&net);
  if(fault<3)CHECK(!ok);else{CHECK(ok);if(fault==3)failPropertyAt=1;else failStartAt=1;CHECK(!s->start(&resources));}
  s->release();CHECK(net.refs==1);table.clear();
 }
 CHECK(objects==0&&locks==0&&admin.refs==1);
 /* Concurrent close and property operations: no use-after-free or double close. */
 resetFaults();{IOService interface,resources;ifnet net;table["en0"]=&net;auto*s=bound(&interface,&net,&resources);IOUserClient*c=nullptr;CHECK(!s->newUserClient(&admin,&admin,0,nullptr,&c));auto*d=request(interface.getRegistryEntryID());std::thread t([&]{caller=&admin;for(unsigned j=0;j<2000;j++){int e=c->setProperties(d);CHECK(e==kIOReturnNoSpace||e==kIOReturnNotOpen);}});c->clientClose();t.join();c->release();d->release();s->stop(&resources);s->release();CHECK(net.refs==1);table.clear();}
 CHECK(objects==0&&locks==0&&admin.refs==1);printf("PASS %u checks: owned live ifnet, exact dictionary, repeated naming refusal, type0/admin/task, stop/close/failure/concurrency cleanup\n",checks);
}
