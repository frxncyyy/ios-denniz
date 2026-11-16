#!/usr/bin/env python3
"""Integer reference for byte UNORM premultiplied source-over; HOST ORACLE ONLY."""

alphas = (0, 64, 128, 192, 255)
dst = (23, 77, 131, 191)  # nonzero destination RGBA
# Premultiplied source RGB; each channel is strictly <= alpha.
def source(a):
    return ((a * 51 + 127) // 255, (a * 102 + 127) // 255,
            (a * 179 + 127) // 255, a)

def unorm_mul(a, b):
    return (a * b + 127) // 255

def over(src, dst):
    inv = 255 - src[3]
    return tuple(min(255, src[i] + unorm_mul(dst[i], inv)) for i in range(4))

def replace(src, dst):
    return src

def straight_alpha_wrong(src, dst):
    # A common accidental second multiplication for premultiplied sources.
    inv = 255 - src[3]
    return tuple(min(255, unorm_mul(src[i], src[3]) + unorm_mul(dst[i], inv)) for i in range(4))

rows=[]
for a in alphas:
    s=source(a); out=over(s,dst)
    assert all(0 <= s[i] <= a for i in range(3))
    assert out == (s[0]+unorm_mul(dst[0],255-a), s[1]+unorm_mul(dst[1],255-a),
                   s[2]+unorm_mul(dst[2],255-a), a+unorm_mul(dst[3],255-a))
    if a not in (0,255):
        assert out != replace(s,dst)
        assert out != straight_alpha_wrong(s,dst)
    if a == 0:
        assert out == dst
    if a == 255:
        assert out == s
    # A BGRA8 render target stores blue, green, red, alpha bytes.
    bgra=bytes((out[2],out[1],out[0],out[3]))
    assert bgra != bytes(out) if out[0] != out[2] else True
    rows.append({"alpha":a,"source_rgba":s,"destination_rgba":dst,
                 "result_rgba":out,"result_bgra_bytes":list(bgra),
                 "replace_rgba":replace(s,dst),"wrong_double_alpha_rgba":straight_alpha_wrong(s,dst)})
assert len(rows)==5 and [r["alpha"] for r in rows]==[0,64,128,192,255]
print("PASS premultiplied source-over pixel oracle: alpha 0/64/128/192/255 over nonzero destination, replacement/double-alpha/BGRA checks; HOST ONLY")
