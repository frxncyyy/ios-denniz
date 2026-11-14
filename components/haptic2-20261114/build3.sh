#!/bin/bash
# haptic2-20261114/build3.sh (q10hapd was built alone first): q10haps (+ 0612 strong), q10hapw (+ 0612 soft), one after another
# through integ/build_all.sh (each waits for the global heavy lock + free RAM inside).
N=$NORD2_WORK/gpu/build/haptic2-20261114
cd $N/integ
bash build_all.sh q10haps "\"0611 $N\" \"0612 $N/strong\"" > $N/build_q10haps.log 2>&1
bash build_all.sh q10hapw "\"0611 $N\" \"0612 $N/soft\"" > $N/build_q10hapw.log 2>&1
echo BUILD3_DONE
