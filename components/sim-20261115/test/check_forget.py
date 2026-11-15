#!/usr/bin/env python3
# static regression: bb_sim_forget()/bb_sim_forget_more() (0710/0711) and every 0710-0714 code path never touch the modem revision /
# IMEI fields (bb_st.rev, bb_st.imei) nor the CGMR/CGSN queries; t_rev_nosim.c's forget() mirrors the cleared field list.
import re, sys
s = open(sys.argv[1]).read()
def body(name):
    i = s.index('static void %s(' % name); j = s.index('\n}\n', i); return s[i:j]
bad = []
for n in ('bb_sim_forget', 'bb_sim_forget_more', 'bb_sim_track'):
    b = body(n)
    for w in ('bb_st.rev', 'bb_st.imei', 'NBM_Q_CGMR', 'NBM_Q_CGSN'):
        if w in b: bad.append((n, w))
for m in re.finditer(r'(bb_st\.rev|bb_st\.imei)\s*(\[[^\]]*\])?\s*=[^=]', s):
    bad.append(('assignment', s[max(0, m.start()-60):m.end()+20].replace('\n', ' ')))
print('CHECK_FORGET_OK' if not bad else 'CHECK_FORGET FAILED %r' % bad); sys.exit(1 if bad else 0)
