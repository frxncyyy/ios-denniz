static int start(unsigned silent,unsigned duration) {
 int e=nau_hw_start(&hardware,48000,2,16,silent,NAH_MAX_GAIN_Q15,duration,now_us());
 if(!e)device.starts++;
 printf("NORD2 AUDIO: start=%d silent=%u duration=%u gate=%u amp=%04x/%04x\n",
 e,silent,duration,hardware.status.gate,hardware.status.amp_revision[0],hardware.status.amp_revision[1]);
 if(e)diagnose("start",e,1);
 return e;
}
