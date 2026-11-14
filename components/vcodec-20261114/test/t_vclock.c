#include "n2vcd_vclock.h"
#include <assert.h>
static int fails, n;
#define CHECK(c) do { n++; if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)
int main(void) {
	uint64_t T = N2VCLK_AUTO_PPS; int A = N2VCLK_AUTO;
	CHECK(n2vclk_want(A,T,1920,1080,30,1)==0);      /* 1080p30 stays 273 */
	CHECK(n2vclk_want(A,T,1920,1080,30000,1001)==0);
	CHECK(n2vclk_want(A,T,1920,1080,60,1)==1);      /* HD60 */
	CHECK(n2vclk_want(A,T,1920,1080,60000,1001)==1);
	CHECK(n2vclk_want(A,T,3840,2160,30,1)==1);      /* 4K30 */
	CHECK(n2vclk_want(A,T,3840,2160,24,1)==1);
	CHECK(n2vclk_want(A,T,1280,720,30,1)==0);
	CHECK(n2vclk_want(A,T,1280,720,60,1)==1);       /* 720p60 */
	CHECK(n2vclk_want(A,T,640,480,30,1)==0);
	CHECK(n2vclk_want(A,T,640,480,60,1)==0);
	CHECK(n2vclk_want(A,T,640,480,120,1)==0);
	CHECK(n2vclk_want(A,T,1920,1080,0,0)==0);       /* fps unknown -> 30 */
	CHECK(n2vclk_want(A,T,1080,1920,30,1)==0);      /* portrait 1080p30 */
	CHECK(n2vclk_want(A,T,1920,1440,30,1)==1);
	CHECK(n2vclk_want(N2VCLK_SLOW,T,3840,2160,60,1)==0);
	CHECK(n2vclk_want(N2VCLK_FAST,T,176,144,15,1)==1);
	CHECK(n2vclk_want(N2VCLK_KEEP,T,3840,2160,60,1)==-1);
	int m=-9; uint64_t t=0;
	CHECK(n2vclk_parse("slow\n",&m,&t)==0 && m==N2VCLK_SLOW);
	CHECK(n2vclk_parse("  fast  ",&m,&t)==0 && m==N2VCLK_FAST);
	CHECK(n2vclk_parse("keep",&m,&t)==0 && m==N2VCLK_KEEP);
	CHECK(n2vclk_parse("auto\r\n",&m,&t)==0 && m==N2VCLK_AUTO && t==T);
	CHECK(n2vclk_parse("auto:50000000",&m,&t)==0 && m==N2VCLK_AUTO && t==50000000ull);
	CHECK(n2vclk_want(m,t,1920,1080,30,1)==1);
	m=7; t=5;
	CHECK(n2vclk_parse("auto:",&m,&t)==-1 && m==7 && t==5);
	CHECK(n2vclk_parse("auto:12x",&m,&t)==-1 && m==7);
	CHECK(n2vclk_parse("auto:5",&m,&t)==-1);
	CHECK(n2vclk_parse("",&m,&t)==-1);
	CHECK(n2vclk_parse("FAST",&m,&t)==-1);
	CHECK(n2vclk_parse(NULL,&m,&t)==-1);
	CHECK(n2vclk_parse("fasterthanlightfasterthanlight!!",&m,&t)==-1);
	CHECK(n2vclk_read_file("/nonexistent/x",&m,&t)==0);
	FILE *f=fopen("/tmp/claude-1000/-home-user/b6fc0670-1b3b-4379-9bef-96b78e4e9ee7/scratchpad/vclk.txt","w"); fputs("slow\n",f); fclose(f);
	CHECK(n2vclk_read_file("/tmp/claude-1000/-home-user/b6fc0670-1b3b-4379-9bef-96b78e4e9ee7/scratchpad/vclk.txt",&m,&t)==1 && m==N2VCLK_SLOW);
	f=fopen("/tmp/claude-1000/-home-user/b6fc0670-1b3b-4379-9bef-96b78e4e9ee7/scratchpad/vclk.txt","w"); fputs("bogus\n",f); fclose(f);
	CHECK(n2vclk_read_file("/tmp/claude-1000/-home-user/b6fc0670-1b3b-4379-9bef-96b78e4e9ee7/scratchpad/vclk.txt",&m,&t)==-1 && m==N2VCLK_SLOW);
	printf("N2TEST VCLOCK %s %d/%d\n", fails?"FAIL":"PASS", n-fails, n);
	return fails!=0;
}
