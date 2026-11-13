#!/usr/bin/env python3
"""wifi-fix-20261114/test/adapt.py TESTDIR: 0491 expectations for a COPY of the 0436 fixture generator (wifi-20261113/test/privmac/make.py,
never the original): the deactivate command carries no address, and a block that drives the 0491 fallback (njs_privmac_verdict_locked /
njs_mac_select_locked, the real binder statics of the owner fixture) through every transition."""
import sys
from pathlib import Path
p=Path(sys.argv[1])/'privmac/make.py';t=p.read_text()
def sub(old,new):
    global t
    assert t.count(old)==1,(old[:80],t.count(old));t=t.replace(old,new)
sub("if(PRIVMAC_MODE==2)assert(!memcmp(deactivate_mac,factory,6));","if(PRIVMAC_MODE==2)assert(!memcmp(deactivate_mac,(const uint8_t[6]){0},6)); /* 0491: gen4m deactivate has no address */")
BLOCK=r'''
 /* ---- 0491: private-address fallback, on the binder's own state ---- */
 {struct stc*t=&njs_native.driver.owner.transaction;struct stc saved=*t;unsigned joined=njs_native.joined,has=njs_native.has_self,mode=niwj_privmac_mode,sw=nss_native.mac_switch,ll=nss_native.lladdr_pending,att=nss_native.mac_attempts,v;
  uint8_t factory[6],mac0[6],old0[6],want0[6];memcpy(factory,nss_native.factory_mac,6);memcpy(mac0,nss_native.info.mac,6);memcpy(old0,nss_native.lladdr_old,6);memcpy(want0,njs_native.want_self,6);
  IOLockLock(nss_native.lock);assert(!njs_privmac_state&&!memcmp(mac0,factory,6));
#define ATTEMPT(on_private,j,auth,assoc,out) (memcpy(nss_native.info.mac,(on_private)?private_mac:factory,6),njs_native.joined=(j),t->auth_attempts=(auth),t->assoc_attempts=(assoc),t->outcome=(out),njs_privmac_verdict_locked())
#define ADMIT() (njs_native.has_self=1,memcpy(njs_native.want_self,private_mac,6),nss_native.mac_switch=0,nss_native.lladdr_pending=0,njs_mac_select_locked())
  /* kill switch from a private address: the way back is deactivate + activate (a lone activate is ignored by the firmware) */
  niwj_privmac_mode=3;memcpy(nss_native.info.mac,private_mac,6);ADMIT();assert(nss_native.mac_switch==2&&nss_native.lladdr_pending&&!memcmp(nss_native.info.mac,factory,6));
  niwj_privmac_mode=PRIVMAC_MODE;
  /* attempts on a private address that are no evidence against it */
  v=ATTEMPT(1,1,1,1,STC_DISCONNECTED);assert(v==(1u|2u|4u)&&!njs_privmac_state);       /* joined, later left */
  v=ATTEMPT(1,0,1,1,STC_FAILED);assert(v==(1u|2u|4u)&&!njs_privmac_state);             /* association request went out: authentication passed (wrong key etc.) */
  v=ATTEMPT(1,0,1,0,STC_CANCELED);assert(v==(1u|2u)&&!njs_privmac_state);              /* user cancel */
  v=ATTEMPT(1,0,0,0,STC_FAILED);assert(v==(1u|2u)&&!njs_privmac_state);                /* failed before any authentication frame */
  v=ATTEMPT(0,0,1,0,STC_FAILED);assert(!v&&!njs_privmac_state);                        /* board address: nothing to say */
  /* the q10jia hardware case: authentication frame never acknowledged -> suspected */
  v=ATTEMPT(1,0,1,0,STC_FAILED);assert(v==(1u|2u|8u|1u<<6)&&njs_privmac_state==1);
  v=ATTEMPT(1,0,1,0,STC_TIMEOUT);assert(v==(1u|2u|8u|1u<<4|1u<<6)&&njs_privmac_state==1);
  /* next admission of the same private request: board address, reached with deactivate + activate (or mode 1's lone activate) */
  memcpy(nss_native.info.mac,private_mac,6);ADMIT();assert(!memcmp(nss_native.info.mac,factory,6)&&!memcmp(nss_native.lladdr_old,private_mac,6)&&nss_native.lladdr_pending&&nss_native.mac_switch==(PRIVMAC_MODE==1?1u:2u));
  /* the board address fails the same way: the AP is not there, suspicion dropped, private addresses again */
  v=ATTEMPT(0,0,1,0,STC_FAILED);assert(v==(1u|8u|1u<<4)&&!njs_privmac_state);
  ADMIT();assert(!memcmp(nss_native.info.mac,private_mac,6)&&nss_native.mac_switch==(PRIVMAC_MODE==1?1u:2u));
  /* suspected again; a board attempt that is canceled decides nothing; one that passes authentication confirms */
  v=ATTEMPT(1,0,1,0,STC_FAILED);assert(njs_privmac_state==1);
  v=ATTEMPT(0,0,1,0,STC_CANCELED);assert(!v&&njs_privmac_state==1);
  v=ATTEMPT(0,0,1,1,STC_FAILED);assert(v==(1u|4u|1u<<4|2u<<6)&&njs_privmac_state==2);
  /* confirmed: private requests stay on the board address whatever happens next */
  ADMIT();assert(!memcmp(nss_native.info.mac,factory,6)&&!nss_native.mac_switch&&!nss_native.lladdr_pending);
  v=ATTEMPT(0,0,1,0,STC_FAILED);assert(!v&&njs_privmac_state==2);v=ATTEMPT(0,1,1,1,STC_DISCONNECTED);assert(!v&&njs_privmac_state==2);
  /* kern.n2wifi_privmac_state=0: tried again */
  njs_privmac_state=0;ADMIT();assert(!memcmp(nss_native.info.mac,private_mac,6)&&nss_native.mac_switch==(PRIVMAC_MODE==1?1u:2u));
  /* suspected + joined on the board address also confirms */
  v=ATTEMPT(1,0,1,0,STC_FAILED);v=ATTEMPT(0,1,1,1,STC_AUTHORIZED);assert(njs_privmac_state==2);
#undef ATTEMPT
#undef ADMIT
  njs_privmac_state=0;*t=saved;njs_native.joined=joined;njs_native.has_self=has;memcpy(njs_native.want_self,want0,6);niwj_privmac_mode=mode;
  nss_native.mac_switch=sw;nss_native.lladdr_pending=ll;nss_native.mac_attempts=att;memcpy(nss_native.info.mac,mac0,6);memcpy(nss_native.lladdr_old,old0,6);IOLockUnlock(nss_native.lock);
 }
'''
A="assert(!nss_native.mac_switch&&!memcmp(nss_native.info.mac,nss_native.factory_mac,6));IOLockUnlock(nss_native.lock);"
sub(A,A+BLOCK.replace('\\','\\\\') if False else A+BLOCK)
sub("decoder, admission, activate-control before the scan, peer and keys; synthetic hardware only","decoder, admission, activate-control before the scan, peer and keys, 0491 fallback transitions; synthetic hardware only")
p.write_text(t);print('adapted',p)
