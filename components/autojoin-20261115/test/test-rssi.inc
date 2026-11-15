/* 0790 host test: actual production IOUC (CI wifi_ios_client.cpp of the stage) + CI niw_decode / niwj_state_reply (renamed as in the
 * archive copy: niv_decode / niwk_state_reply) + the wifi_rssi_bounds.c wrappers. */
extern "C" int n2rb_threshold;
static int32_t g32s(const uint8_t*p){return (int32_t)((uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24);}
int main(){assertNoJoins();IOUserClient*c=make();IOExternalMethodArguments a={};uint8_t raw[64];unsigned checks=0;
 auto reset=[&](uint32_t request,unsigned extent,unsigned capacity){envelope(carrier,NIW_GET,request,extent);memset(raw,0xa5,sizeof raw);a={};a.structureInput=carrier;a.structureInputSize=sizeof carrier;a.structureOutput=raw;a.structureOutputSize=capacity;};
 auto unchanged=[&](){for(auto b:raw)assert(b==0xa5);};
 /* 1: GET 106 (RSSI_BOUNDS, 16 bytes) answers {1,-19,65,threshold}, nothing else written */
 reset(106,16,16);IOReturn r=call(c,0,a);printf("GET106 status=%#x used=%u\n",(unsigned)r,(unsigned)a.structureOutputSize);
 assert(r==kIOReturnSuccess&&a.structureOutputSize==16);
 assert(g32s(raw)==1&&g32s(raw+4)==-19&&g32s(raw+8)==65&&g32s(raw+12)==-80);for(unsigned i=16;i<sizeof raw;i++)assert(raw[i]==0xa5);checks++;
 /* 2: other thresholds follow the sysctl variable */
 n2rb_threshold=-72;reset(106,16,16);assert(!call(c,0,a)&&g32s(raw+12)==-72);checks++;n2rb_threshold=-80;
 /* 3: kill switch 0 (and nonsense positive / below -127) = unsupported exactly as before 0790 */
 for(int t:{0,7,-128}){n2rb_threshold=t;reset(106,16,16);r=call(c,0,a);assert(r==kIOReturnUnsupported);unchanged();checks++;}n2rb_threshold=-80;
 /* 4: capacity below 16 -> refused, output untouched */
 for(unsigned n=0;n<16;n++){reset(106,16,n);r=call(c,0,a);assert(r!=kIOReturnSuccess);unchanged();checks++;}
 /* 5: GET 106 with another payload size stays unsupported (not remapped) */
 for(unsigned n:{0u,12u,15u,17u,24u,52u}){reset(106,n,64);r=call(c,0,a);assert(r==kIOReturnUnsupported);unchanged();checks++;}
 /* 6: the real RSSI request (16, 52 bytes) is unchanged: CI state reply, no association -> ENXIO 6 */
 reset(16,52,64);r=call(c,0,a);assert(r==(IOReturn)6);unchanged();checks++;
 /* 7: a wifid-shaped RSSI request can never be (16,16) except through the 106 remap: (16,16) sent directly also gets bounds (documented) */
 reset(16,16,16);r=call(c,0,a);assert(r==kIOReturnSuccess&&g32s(raw+12)==-80);checks++;
 /* 8: other CI requests unchanged: CARD 12, STATE 13, SSID 1 (ENXIO), unsupported 18 */
 reset(12,13,13);assert(!call(c,0,a)&&a.structureOutputSize==13);checks++;
 reset(13,0,4);assert(!call(c,0,a)&&a.structureOutputSize==4&&g32s(raw)==0);checks++;
 reset(1,32,32);assert(call(c,0,a)==(IOReturn)6);checks++;
 reset(18,0,0);assert(call(c,0,a)==kIOReturnUnsupported);checks++;
 /* 9: SET 106 is not touched (operation 0x802869c8 path) */
 envelope(carrier,0x802869c8,106,16);a={};a.structureInput=carrier;a.structureInputSize=sizeof carrier;assert(call(c,0,a)==kIOReturnUnsupported);checks++;
 /* 10: wrong interface name still refused before anything */
 reset(106,16,16);memcpy(carrier+15368,"bad",4);assert(call(c,0,a)!=kIOReturnSuccess);unchanged();checks++;
 assertNoJoins();assert(!c->clientClose());c->release();assert(!rights&&!locks&&!malloc_count&&!buffer_count&&ownerTask.refs==1);
 printf("PASS 0790 GET106 RSSI_BOUNDS {1,-19,65,thr} through the actual IOUC, sysctl/kill switch, capacity/shape guards, RSSI 52/CARD/STATE/SSID/SET unchanged; checks=%u\n",checks);return 0;
}
