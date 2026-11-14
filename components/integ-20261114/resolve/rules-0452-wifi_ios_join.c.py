# 0452 (switch bits: niwj_8021x&1 = 802.1X decode) onto the chain's merged profile line (0434 'personal').
N = 1
def res(k, a, b, c):
    assert k == 0 and len(a) == 2 and len(c) == 2 and c[1] == b[1] and a[0] == b[0]
    return [c[0], a[1]]
