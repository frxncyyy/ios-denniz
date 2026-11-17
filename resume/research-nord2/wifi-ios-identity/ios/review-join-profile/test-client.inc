int main(){IOMemoryDescriptor*d,*s;IOUserClient*c=armed(&d,&s);IOExternalMethodArguments a={};auto setup=[&]{joinWire();auto*p=carrier+8;p[464]=72;p[508]=2;put(p+536,36);memset(p+540,'x',36);p[576]=0;a={};a.structureInput=carrier;a.structureInputSize=sizeof carrier;};
 setup();ownerUnavailable=1;assert(call(c,0,a)==kIOReturnNotReady&&!jactive&&!jenqueues);ownerUnavailable=0;
 setup();carrier[8+509]=1;assert(call(c,0,a)==kIOReturnUnsupported&&!jactive&&!jenqueues);
 setup();carrier[8+575]=0;assert(call(c,0,a)==kIOReturnUnsupported&&!jactive&&!jenqueues);
 setup();assert(!call(c,0,a)&&jactive&&jenqueues==1&&copiedJoin.native_flags==4&&copiedJoin.pmk[31]==31);
 assert(!c->clientClose());c->release();joinNotice(NJS_RETIRED);d->release();s->release();assert(!malloc_count&&!buffer_count&&!rights&&ownerTask.refs==1);
 puts("PASS actual IOUC envelope: unavailable/mismatched owner and malformed scope refuse; exact retained identity enqueues once; close retires owned credential copy");return 0;}
