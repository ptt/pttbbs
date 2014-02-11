#!/usr/bin/env python
# Usage: ./big5_gen.py > big5_tbl.py

import os

COMMON_SYS = '../../common/sys'
B2U_FILE = os.path.join(COMMON_SYS, 'uao250-b2u.big5.txt')
U2B_FILE = os.path.join(COMMON_SYS, 'uao250-u2b.big5.txt')

# b2u
b2u = open(B2U_FILE, 'r').readlines()
b2u = [line.strip().split(' ')
       for line in b2u
       if line.strip().startswith('0x')]
b2u = dict((int(b, 0), int(u, 0)) for (b, u) in b2u)

print """#!/usr/bin/env python

"""
print "b2u_table = ("
for i in range(0x10000):
    print '0x%04x,' % (i if i not in b2u else b2u[i]),
    if i % 10 == 9:
        print ''
print ")\n"

# u2b
u2b = open(U2B_FILE, 'r').readlines()
u2b = [line.strip().split(' ')
       for line in u2b
       if line.strip().startswith('0x')]
u2b = dict((int(u, 0), int(b, 0)) for (b, u) in u2b)

print "u2b_table = ("
for i in range(0x10000):
    print '0x%04x,' % (i if i not in u2b else u2b[i]),
    if i % 10 == 9:
        print ''
print ")\n"
