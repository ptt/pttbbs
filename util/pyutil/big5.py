#!/usr/bin/env python

from __future__ import unicode_literals
from builtins import bytes, chr

from . import big5_tbl

def decode(s, strip_zero=True):
    ret = ''
    b = 0
    for i in s:
        if b:
            b = (b << 8) | (ord(i))
        else:
            b = ord(i)
            if b >= 0x80:
                continue
        if (b == 0) and strip_zero:
            break
        ret += chr(big5_tbl.b2u_table[b])
        b = 0
    return ret

def encode(u):
    ret = ''
    for i in u:
        c = big5_tbl.u2b_table[ord(i)]
        b0 = (c) & 0xff
        b1 = (c >> 8) & 0xff
        if b1:
            ret = ret + chr(b1)
        ret = ret + chr(b0)
    return ret
