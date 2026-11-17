/* PRIVATE candidate ONLY, not compiled/linked into active kernel.
 * Place after nw_restore()/before ni_wifi_power_probe; include snapshot.h.
 * Must remain serialized within existing ni_wifi_power_probe, never worker. */
struct nwc_adapter {vm_offset_t rgu,host;uint32_t id;};
static int nwc_adapter_check(void *opaque,unsigned required)
{
 struct nwc_adapter *c=opaque;
 if(required!=NWC_REQUIRED || !nw.saved || !nw.domain_touched || c->id!=0x20010101 ||
  !nw.spm || !nw.infra || !nw.gals || !c->rgu || !c->host)return -1;
 /* Existing AP registers only. Never touch CONN if an AP prerequisite failed. */
 if((ni_rd(nw.spm,0x304)&0x1f)!=0xd || !(ni_rd(nw.spm,0x16c)&2) ||
  !(ni_rd(nw.spm,0x170)&2) || !(ni_rd(nw.gals,0x504)&1) ||
  (ni_rd(nw.infra,0x228)&0x6000) || (ni_rd(nw.infra,0x724)&4))return -1;
 return (ni_rd(c->host,0x184)&0x20)?-1:0;
}
static int nwc_adapter_read(void *opaque,uint64_t address,uint32_t *value)
{
 struct nwc_adapter *c=opaque;
 if(!value)return -1;
 switch(address) {
 case 0x18000010:*value=ni_rd(c->rgu,0x10);return 0;
 case 0x18000000:*value=ni_rd(c->rgu,0);return 0;
 case 0x180602cc:*value=ni_rd(c->host,0x2cc);return 0;
 default:return -1;
 }
}
static void nwc_power_window_snapshot(vm_offset_t host,uint32_t id)
{
 unsigned enabled=0;PE_parse_boot_argn("nord2wificold",&enabled,sizeof(enabled));
 if(enabled!=1)return; /* nord2wifi=2 outer gate still mandatory. */
 /* Call site is AFTER cfg_id read, BEFORE success=true/out/nw_restore. */
 if(!nw.saved || !nw.domain_touched || id!=0x20010101)return;
 struct nwc_adapter c={ml_io_map(0x18000000,0x1000),host,id};
 const struct nwc_ops ops={nwc_adapter_check,nwc_adapter_read};struct nwc_result r;
 int status=nwc_snapshot(&ops,&c,1,&r);
 printf("NORD2 WIFI COLD: status=%d valid=%#x attempted=%u error=%d cpu_reset=%#x top=%#x power_status=%#x cold=%d\n",
  status,r.valid_mask,r.attempted,r.error,r.raw[0],r.raw[1],r.raw[2],r.cold);
 /* Do not return from the outer probe or bypass its out/nw_restore logic. */
}
