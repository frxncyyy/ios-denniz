#include "test-xnu.h"
#include AKS_SOURCE
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"failure line %u\n",__LINE__); std::abort(); } } while(0)
int main() {
 AppleKeyStoreUserClient client;
 uint64_t inputs[2]={0,UINT64_C(0x123456789a070007)},klass=0;
 uint8_t key[32],wrapped[40],plain[32],scratch[72];
 for (unsigned i=0;i<32;++i) key[i]=(uint8_t)i;
 IOExternalMethodArguments a;
 a.selector=10;a.scalarInput=inputs;a.scalarInputCount=2;a.scalarOutput=&klass;a.scalarOutputCount=1;
 a.structureInput=key;a.structureInputSize=32;a.structureOutput=wrapped;a.structureOutputSize=40;
 CHECK(client.externalMethod(10,&a,nullptr,nullptr,nullptr)==0);CHECK(klass==inputs[1]);CHECK(a.structureOutputSize==40);
 // Opaque class high bits are not authenticated or encoded by this upstream format.
 inputs[1]^=UINT64_C(0x100000000);
 a.selector=11;a.scalarOutputCount=0;a.structureInput=wrapped;a.structureInputSize=40;a.structureOutput=plain;a.structureOutputSize=32;
 CHECK(client.externalMethod(11,&a,nullptr,nullptr,nullptr)==0);CHECK(!memcmp(key,plain,32));
 // Demonstrate rather than hide the format's lack of ciphertext authentication.
 wrapped[17]^=0x80;a.structureOutputSize=32;
 CHECK(client.externalMethod(11,&a,nullptr,nullptr,nullptr)==0);CHECK(plain[9]==(uint8_t)(key[9]^0x80));wrapped[17]^=0x80;
 // Inactive in-band storage is never dereferenced when the descriptor is active.
 a.structureOutput=(void*)1;a.structureOutputSize=0;IOMemoryDescriptor md;a.structureOutputDescriptor=&md;a.structureOutputDescriptorSize=128;
 a.asyncReference=(uint64_t*)1; // Count0 is inactive storage too.
 CHECK(sweet_aks_compat_key(11,&a,scratch)==0);CHECK(!memcmp(md.data,key,32));CHECK(a.structureOutputDescriptorSize==32);
 for (auto x:scratch) CHECK(x==0);
 // Foreign recognized-marker, reserved/class/short malformed formats refuse before write.
 for(unsigned i:{0u,4u,5u,6u,7u}) {
  wrapped[i]^=1;a.structureOutputDescriptorSize=128;unsigned writes=md.writes;
  CHECK(sweet_aks_compat_key(11,&a,scratch)==kIOReturnBadArgument);CHECK(md.writes==writes);CHECK(a.structureOutputDescriptorSize==128);
  for(auto x:scratch) CHECK(x==0);
  wrapped[i]^=1;
 }
 std::printf("PASS independent inactive-storage/opaque-class/no-authentication/foreign-header probes: %u checks\n",checks);
}
