/* SPDX-License-Identifier: MIT
 * 0282: in-place upgrade of a committed RedwoodAudio install (compiled-in switch NORD2_AUDIO_UPGRADE).
 *
 * The install journal J and commit marker C stay byte-identical (their digest is what the rollback
 * machinery and C are keyed on). The installed package's sizes/hashes are instead overridden by a
 * package record K bound to J's digest; every boot reads J, then applies K, so custom(), restore()
 * and the mode=1 package check all verify against the upgraded files and rollback keeps working.
 *
 * Steps (each durable before the next; every boot resumes from the observed state):
 *   stage   U/RedwoodAudio.driver{,/RedwoodAudio,/Info.plist}   new package, verified + synced
 *   intent  KN = record(new)                                     the roll-forward point
 *   swap    HAL/RedwoodAudio.driver -> U/RedwoodAudio.driver.previous; U/RedwoodAudio.driver -> HAL/
 *   commit  remove K; KN -> K
 * Failure before intent: the old install stays as is and boot continues. After intent: the next boot
 * rolls forward; anything not matching one of the known states is refused like any other corruption.
 * The previous driver is kept in U until the next upgrade starts.
 */
struct pkgrec {
 unsigned char magic[8]; uint32_t version, generation;
 unsigned char journal_digest[32];
 uint32_t executable_size, plist_size;
 unsigned char executable_hash[32], plist_hash[32], digest[32];
};
_Static_assert(sizeof(struct pkgrec)==152,"package record layout");
static void rec_from_journal(const struct journal *j,struct pkgrec *r)
{
 memset(r,0,sizeof(*r));r->executable_size=j->executable_size;r->plist_size=j->plist_size;
 memcpy(r->executable_hash,j->executable_hash,32);memcpy(r->plist_hash,j->plist_hash,32);
}
static void rec_to_journal(struct journal *j,const struct pkgrec *r)
{
 j->executable_size=r->executable_size;j->plist_size=r->plist_size;
 memcpy(j->executable_hash,r->executable_hash,32);memcpy(j->plist_hash,r->plist_hash,32);
}
static int rec_same(const struct pkgrec *a,const struct pkgrec *b)
{
 return a->executable_size==b->executable_size&&a->plist_size==b->plist_size&&
 !memcmp(a->executable_hash,b->executable_hash,32)&&!memcmp(a->plist_hash,b->plist_hash,32);
}
static int rec_read(enum path p,const struct journal *j,struct pkgrec *r)
{
 int e=read_file(p,r,sizeof(*r),0600);if(e)return e;
 if(memcmp(r->magic,"N2APKG1",8)||r->version!=1||memcmp(r->journal_digest,j->digest,32)||
 !r->executable_size||!r->plist_size||r->executable_size>N2AP_MAX||
 r->plist_size>N2AP_MAX-r->executable_size)return EINVAL;
 return check_hash((void *)r,offsetof(struct pkgrec,digest),r->digest);
}
/* Exact driver directory: two entries, exact sizes, modes and hashes. */
static int driver_ok(enum path d,enum path x,enum path p,const struct pkgrec *r)
{
 int e=entries(d,"RedwoodAudio","Info.plist");if(e)return e;
 e=read_file(x,buffer,r->executable_size,0755);if(e)return e;
 e=check_hash(buffer,r->executable_size,r->executable_hash);if(e)return e;
 e=read_file(p,buffer,r->plist_size,0644);if(e)return e;
 return check_hash(buffer,r->plist_size,r->plist_hash);
}
static int remove_driver(enum path d,enum path x,enum path p)
{
 int e=remove_fixed(x,0);if(e&&e!=ENOENT)return e;
 e=remove_fixed(p,0);if(e&&e!=ENOENT)return e;
 e=remove_fixed(d,1);return e==ENOENT?0:e;
}
static int move_any(enum path from,enum path to)
{struct identity id;int e=identity(from,&id);return e?e:move(from,to,&id);}
/* Applies K over the journal's package fields. Absent K: the journal's own package. */
static int upgrade_overlay(struct journal *j)
{
 struct pkgrec r;int e=rec_read(K,j,&r);
 if(e==ENOENT)return 0;if(e)return e;
 rec_to_journal(j,&r);return 0;
}
/* Rolls a durable intent (KN) forward from whichever swap state the last boot stopped in. */
static int upgrade_resume(struct journal *j)
{
 struct pkgrec cur,n;int e,step=0;
 memset(&n,0,sizeof(n));e=rec_read(KN,j,&n);
 if(e==ENOENT)return 0;
 rec_from_journal(j,&cur);
 if(e){/* torn intent: no swap starts before KN is verified, so the old driver must be intact */
 step=1;if((e=same(A,&j->custom))||(e=driver_ok(AD,AE,AP,&cur)))goto out;
 e=remove_fixed(KN,0);if(!e)e=sync_path(P);goto out;}
 if((e=same(A,&j->custom)))goto out;
 if(driver_ok(AD,AE,AP,&n)){
 if(absent(AD)!=0){step=2;   /* AD present: before the swap */
 if((e=driver_ok(AD,AE,AP,&cur))||(e=absent(UO))||(e=move_any(AD,UO)))goto out;}
 step=3;                  /* halfway: old driver parked, new one staged */
 if((e=driver_ok(UO,UOE,UOP,&cur))||(e=driver_ok(UN,UNE,UNP,&n))||(e=move_any(UN,AD))||
 (e=driver_ok(AD,AE,AP,&n)))goto out;
 }
 step=4;                  /* new driver active: publish the record */
 {int k=remove_fixed(K,0);if(k&&k!=ENOENT){e=k;goto out;}}
 if((e=move_any(KN,K))||(e=sync_path(P)))goto out;
 rec_to_journal(j,&n);
 out:
 printf("NORD2 AUDIO UPGRADE: resume step=%d error=%d generation=%u\n",step,e,e?0u:n.generation);
 return e;
}
/* Starts an upgrade when the DT package differs from the installed one. Errors before the intent is
 * durable leave the verified old install in place and return 0 (*skipped set). */
static int upgrade_start(struct journal *j,const struct n2ap_view *pkg,int *skipped)
{
 struct pkgrec cur,n,old;int e,step=1;*skipped=0;
 rec_from_journal(j,&cur);memset(&n,0,sizeof(n));
 n.executable_size=pkg->executable.length;n.plist_size=pkg->plist.length;
 hash(pkg->executable.bytes,n.executable_size,n.executable_hash);hash(pkg->plist.bytes,n.plist_size,n.plist_hash);
 if(rec_same(&cur,&n))return 0;
 memcpy(n.magic,"N2APKG1",8);n.version=1;memcpy(n.journal_digest,j->digest,32);
 n.generation=rec_read(K,j,&old)?1:old.generation+1;
 hash(&n,offsetof(struct pkgrec,digest),n.digest);
 /* stage */
 if((e=driver_ok(AD,AE,AP,&cur))||(e=absent(KN)))goto skip;
 step=2;if((e=remove_driver(UO,UOE,UOP))||(e=remove_driver(UN,UNE,UNP)))goto skip;
 if(absent(U)){if((e=directory(U)))goto skip;}else if((e=mkdir_fixed(U)))goto skip;
 step=3;if((e=mkdir_fixed(UN))||
 (e=create_file(UNE,pkg->executable.bytes,n.executable_size,0755))||
 (e=create_file(UNP,pkg->plist.bytes,n.plist_size,0644))||
 (e=driver_ok(UN,UNE,UNP,&n))||(e=sync_path(UN))||(e=sync_path(U))||(e=sync_path(P)))goto skip;
 /* intent */
 /* A KN that failed readback is torn and resume() drops it next boot; a complete one whose
  * parent sync failed is rolled forward next boot. Either way this boot keeps the old driver. */
 step=4;if((e=create_file(KN,&n,sizeof(n),0600))||(e=sync_path(P)))goto skip;
 printf("NORD2 AUDIO UPGRADE: staged generation=%u executable=%u plist=%u\n",n.generation,n.executable_size,n.plist_size);
 return upgrade_resume(j);
 skip:
 printf("NORD2 AUDIO UPGRADE: not started step=%d error=%d; keeping the installed package\n",step,e);
 *skipped=1;return 0;
}
