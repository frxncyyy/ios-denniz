from pathlib import Path
import tempfile,shutil,subprocess,json,hashlib
R=Path('$NORD2_REPO');P=R/'research/nord2/wifi/ios-service-bridge/ifnet-native-candidate';O=P/'independent-review-work';O.mkdir(exist_ok=True)
m=json.loads((P/'manifest.json').read_text())
def verify():
 for f in m['files']:assert hashlib.sha256((P/f['path']).read_bytes()).hexdigest()==f['sha256'],f['path']
verify()
with tempfile.TemporaryDirectory(prefix='nwil-independent-') as t:
 d=Path(t);a=d/'xnu/iokit/Kernel/arm';b=d/'xnu/bsd/dev';a.mkdir(parents=True);b.mkdir(parents=True)
 for f in (R/'shim/nord2-xnu').glob('*.h'):shutil.copy2(f,b/f.name)
 for n in ('wifi_ifnet_link.h','wifi_ifnet_link.inc'):shutil.copy2(P/n,b/n)
 for n in ('wifi_ios_registry.cpp','test-registry.cpp','test-registry-xnu.h'):shutil.copy2(P/n,a/n)
 flags=['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all']
 subprocess.run([*flags,str(a/'test-registry.cpp'),'-o',str(d/'test')],check=True)
 z=subprocess.run([str(d/'test')],capture_output=True,text=True,check=True,timeout=20);(O/'author-host-rerun.log').write_text(z.stdout+z.stderr)
 h=a/'test-registry-xnu.h';s=h.read_text();s=s.replace('if(eventHook)eventHook();','auto retainedHook=eventHook;if(retainedHook)retainedHook();');s=s.replace('static int ifnet_input(ifnet_t net,mbuf_t p,ifnet_stat_increment_param*stats){','static std::function<void()> inputHook;\nstatic int ifnet_input(ifnet_t net,mbuf_t p,ifnet_stat_increment_param*stats){if(inputHook)inputHook();');h.write_text(s)
 q=a/'test-registry.cpp';s=q.read_text().replace('int main(){','static int author_main(){');i=s.rfind('}');s=s[:i]+'return 0;\n'+s[i:]
 s+='''
int main(){author_main();reset();provider.setProperty("compatible","nord2,mt6893-wifi",sizeof("nord2,mt6893-wifi"));IO80211Controller controller;assert(controller.start(&provider));ifnet_t net=lastNet;auto arp=ethernet(0x0806);auto ip=ethernet(0x0800);unsigned before=sentCalls;
 inputHook=[&]{assert(!nwil_output(net,packet(arp,5)));};assert(!nord2_wifi_ifnet_link(33,1));assert(!nord2_wifi_ifnet_receive(33,ip.data(),ip.size()));inputHook=nullptr;assert(sentCalls==before+1&&net->refs==3);
 eventHook=[&]{eventHook=nullptr;assert(!nord2_wifi_ifnet_link(33,0));};assert(!nord2_wifi_ifnet_link(33,0));assert(!nwil_active(net)&&!(net->flags&IFF_RUNNING));eventHook=nullptr;
 assert(!nord2_wifi_ifnet_link(34,1));int refs=net->refs;eventHook=[&]{eventHook=nullptr;nwil_unbind(net);assert(net->refs==refs);assert(nwil_output(net,packet(arp))==ENETDOWN);};assert(!nord2_wifi_ifnet_link(34,0));assert(!nwil.net&&net->refs==2);assert(!nwil_bind(net)&&net->refs==3);eventHook=nullptr;
 assert(!nord2_wifi_ifnet_link(35,1));assert(nord2_wifi_ifnet_link(34,0)==ESTALE);assert(nwil_active(net));controller.stop(&provider);assert(!nwil.net&&allocated==released);IOLockFree(nwil_lock);nwil_lock=nullptr;assert(!locks);puts("PASS independent ifnet: input callback reentrant output, nested link event, unbind during event with retained ifnet, stale-down preservation, balanced teardown");return 0;}
''';q.write_text(s)
 subprocess.run([*flags,str(q),'-o',str(d/'probe')],check=True)
 z=subprocess.run([str(d/'probe')],capture_output=True,text=True,timeout=20);(O/'independent-probes.log').write_text(z.stdout+z.stderr);print(z.stderr[-2500:]);z.check_returncode()
verify();print('PASS isolated author host rerun + independent callback/unbind/generation/refcount probes; frozen pins unchanged')
