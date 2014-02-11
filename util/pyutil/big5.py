#!/usr/bin/env python

import big5_tbl

def decode(s, strip_zero=True):
    ret = u''
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
	ret += unichr(big5_tbl.b2u_table[b])
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
