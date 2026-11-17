#!/usr/bin/env python3
"""Generate private expanded registry and reviewable patch; never write canonical."""
from pathlib import Path
import difflib,hashlib
r=Path(__file__).resolve().parent
root=r.parents[4]
p=root/'shim/nord2-xnu/wifi_ios_registry.cpp';assert hashlib.sha256(p.read_bytes()).hexdigest()=='2a38f9c80acdb62536b11f59dd5947fe24d280142cbf763c295a2a5198d286e0','canonical base changed; rebase explicitly';original=p.read_text();s=original
s=s.replace('#include <libkern/c++/OSData.h>','#include <libkern/c++/OSData.h>\n#include <libkern/c++/OSNumber.h>\n#include <kern/task.h>\n#include <kern/clock.h>')
s=s.replace('class IO80211Controller : public IOService {',(r/'network-stack.inc').read_text()+'\nclass IO80211Controller : public IOService {')
s=s.replace('ifnet_t net=nullptr;IO80211SkywalkInterface*interface=nullptr;','ifnet_t net=nullptr;IO80211SkywalkInterface*interface=nullptr;IONetworkStack *stack=nullptr;')
s=s.replace('net=nullptr;interface=nullptr;unsigned opt=0;','net=nullptr;interface=nullptr;stack=nullptr;unsigned opt=0;')
s=s.replace('!interface->setProperty("IOPrimaryInterface",true))','!interface->setProperty("IOPrimaryInterface",true)||!interface->setProperty("IOInterfaceState",(uint64_t)1,32))')
s=s.replace(' interface->registerService();registerService();printf(',''' /* Publish only after the existing real ifnet and its interface identity are valid.
  * The stack owns an additional lookup reference, never another ifnet. */
 IOService *resources=IOService::getResourceService();stack=new IONetworkStack;
 bool stackAttached=false,stackStarted=false;
 if(stack&&resources&&stack->initOwned(interface,net)){
  stackAttached=stack->attach(resources);if(stackAttached)stackStarted=stack->start(resources);
 }
 if(!stackStarted){
  if(stack){if(stackAttached)stack->detach(resources);stack->release();stack=nullptr;}
  interface->stop(this);interface->detach(this);interface->release();interface=nullptr;
  ifnet_detach(net);net=nullptr;IOService::stop(p);return false;
 }
 interface->registerService();stack->registerService();registerService();
 printf("NORD2 WIFI IOS STACK: registered=1 unit=%u ifstate=1 t_ms=%llu\\n",ifnet_unit(net),niws_ms());printf(''')
s=s.replace('void IO80211Controller::stop(IOService*p){','''void IO80211Controller::stop(IOService*p){
 /* Invalidate the stack before removing the identity or detaching BSD. */
 if(stack){stack->deactivate();stack->terminate();stack->detach(IOService::getResourceService());stack->release();stack=nullptr;}
 if(interface)interface->removeProperty("IOInterfaceState");''')
(r/'wifi_ios_registry.cpp').write_text(s)
(r/'registry.patch').write_text(''.join(difflib.unified_diff(original.splitlines(True),s.splitlines(True),fromfile='a/shim/nord2-xnu/wifi_ios_registry.cpp',tofile='b/shim/nord2-xnu/wifi_ios_registry.cpp')))
