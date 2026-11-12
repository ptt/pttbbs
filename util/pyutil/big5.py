#!/usr/bin/env python3

if __package__:
    from . import big5_tbl
else:  # for pttpost.py or pttstruct.py run as a script
    import big5_tbl

def decode(s, strip_zero=True):
    """Decodes Big5 (UAO) bytes into a str."""
    ret = ''
    b = 0
    for i in s:
        if b:
            b = (b << 8) | i
        else:
            b = i
            if b >= 0x80:
                continue
        if (b == 0) and strip_zero:
            break
        ret += chr(big5_tbl.b2u_table[b])
        b = 0
    return ret

def encode(u):
    """Encodes a str into Big5 (UAO) bytes."""
    ret = bytearray()
    for i in u:
        c = big5_tbl.u2b_table[ord(i)]
        b0 = (c) & 0xff
        b1 = (c >> 8) & 0xff
        if b1:
            ret.append(b1)
        ret.append(b0)
    return bytes(ret)
