#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "audio_stream.h"
static struct nsa_stream s;
static const int16_t pcm[8]={100,-100,200,-200,300,-300,400,-400};
static void demonstrate(const char *reason){
 printf("%s: written=%llu delivered=%llu dropped=%llu queued=%u unaccounted=%llu\n",reason,
 (unsigned long long)s.written,(unsigned long long)s.delivered,(unsigned long long)s.dropped,s.count,
 (unsigned long long)(s.written-s.delivered-s.dropped-s.count));
 assert(s.written==4 && s.delivered==0 && s.dropped==0 && s.count==0);
}
int main(void){
 nsa_initialize(&s,1);assert(nsa_enqueue(&s,pcm,4,1000000)==4);nsa_volume(&s,0,1);demonstrate("muteflush");
 nsa_initialize(&s,1);assert(nsa_enqueue(&s,pcm,4,1000000)==4);nsa_enable(&s,0);demonstrate("STOPflush");
 nsa_initialize(&s,1);assert(nsa_enqueue(&s,pcm,4,1000000)==4);nsa_flush_idle(&s,1750000);demonstrate("idleflush");
 puts("REPRODUCED: accepted PCM erased by flush is omitted from dropped counter");
}
