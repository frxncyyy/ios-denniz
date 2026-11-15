import sys
sys.argv=[sys.argv[0], sys.argv[1]]
src=open(sys.argv[0].replace('sweep.py','run_cal.py')).read().split('CALR, CALW')[0]
exec(src)
def buf(W):
    a = 8 if W < 0x239 else 5120//W; b = 8 if 2*W < 0x239 else 5120//(2*W)
    n=(a+W-1)//a; m=(W-1+b)//b; n=(n+3)&~3; m=(m+3)&~3
    o=[a,W*4,a*4,a*n,n,b,0,0,b*m,m]
    if o[3]*a>=0x1401: o[0]=a-1
    if 0x1401<=2*(b*o[8]): o[5]=b-1
    return o
bad=0
for W in range(1,2561):
    o=list(call(0x215d34,[1,0,0,0x202042,W-1,0])[:10])
    if o!=buf(W): bad+=1; print('MISMATCH',W,o,buf(W)) if bad<5 else None
print('widths 1..2560 mismatches:',bad)
