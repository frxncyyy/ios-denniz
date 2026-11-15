import sys
src=open(sys.argv[0].replace('sweep_nv12.py','run_cal.py')).read().split('CALR, CALW')[0]
exec(src)
def buf(W):
    a = 4 if W < 0x401 else 5120//W
    n=(a+W-1)//a; m=(a+(W>>1)-1)//a; n=(n+3)&~3; m=(m+3)&~3
    o=[a,W,a,a*n,n,a,W,2*a,a*m,m]
    if o[3]*a>=0x1401: o[0]=a-1
    if 0x1401<=2*(o[5]*o[8]): o[5]=a-1
    return o
bad=0
for W in range(2,5121,1):
    for fmt in (0x4c084c,0x4c086c):
        o=list(call(0x215d34,[1,0,0,fmt,W-1,0])[:10])
        if o!=buf(W):
            bad+=1
            if bad<5: print('MISMATCH',hex(fmt),W,o,buf(W))
print('NV12/NV21 widths 2..5120 mismatches:',bad)
