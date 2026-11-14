# 0450 (802.1X) onto 0430+0434 (SAE)+0436 (private MAC): both decoders kept; an 802.1X request never creates an SAE secret record and
# selects APs by its AKM only (rsn_ok with the 0450 AKM argument); PSK/SAE requests select exactly as 0434 (rsn_ok AKM 2 || rsn_wide).
N = 6
def res(k, a, b, c):
    if k == 0:   # includes: both
        assert a == ['#include "wifi_station_transaction.h"'] and b == [] and c == ['#include "wifi_8021x.h"']; return a + c
    if k == 1:   # profile check + 802.1X branch, SAE's open branch (cached)
        assert a[0].startswith(' if(j16(p+4)!=2') and '!personal' in a[0] and len(a) == 3 and len(c) == 9 and c[2].startswith(' if(j16(p+4)!=2')
        return [c[0], c[1],
                ' if(j16(p+4)!=2||j16(p+6)!=1||(!open&&!personal&&!eap))return NIWJ_PROFILE;'] + c[3:8] + [a[2]]
    if k == 2:   # key material: 802.1X marker first, then SAE's cached-PMK / passphrase order
        assert a[0] == ' if(!open){' and c[0].startswith(' if(eap){n8x_marker') and c[1] == ' else if(!open){'
        return [c[0], c[1]] + a[1:]
    if k == 3:   # candidate(): 0450 statics + 0434 signature (allow)
        assert len(a) == 1 and a[0].endswith('struct parsed*a,unsigned allow){') and len(c) == 3; return c[:2] + a
    if k == 4:   # RSN element of the AP
        assert len(a) == 1 and 'rsn_wide' in a[0] and len(c) == 1
        return [a[0].replace('(!rsn_ok(r->ies+i,z+2)&&!rsn_wide(r->ies+i,z+2,allow))',
                             '(!rsn_ok(r->ies+i,z+2,wanted_akm)&&(wanted_akm!=N8X_AKM_PSK||!rsn_wide(r->ies+i,z+2,allow)))')]
    if k == 5:   # niwj_select: 802.1X marker -> wanted AKM, 0434 allowed families, the loop with allow, 0450 log line
        assert a[-1].lstrip().startswith('for(unsigned i=0;i<s->count;i++)if(candidate(') and c[0].startswith(' /* 0450') and len(c) == 6
        return c[:4] + a + [c[5]]
    raise SystemExit('unexpected block %d' % k)

def post(t):   # an 802.1X request (no passphrase) never leaves an SAE secret record behind (802.1X runs on the e8_ stc copy, no hook)
    old = ' if(!open&&niwj_sae_mode){'
    assert t.count(old) == 1; return t.replace(old, ' if(!open&&!eap&&niwj_sae_mode){')
