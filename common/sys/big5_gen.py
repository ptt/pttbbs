#!/usr/bin/env python

# b2u
b2u = open('uao250-b2u.big5.txt', 'r').readlines()
b2u = [line.strip().split(' ')
       for line in b2u
       if line.strip().startswith('0x')]
b2u = dict((int(b, 0), int(u, 0)) for (b, u) in b2u)

print """#include <stdint.h>
extern const uint16_t const b2u_table[];
extern const uint16_t const u2b_table[];
extern const uint8_t const b2u_ambiguous_width[];
"""
print "const uint16_t const b2u_table[0x10000] = {"
for i in range(0x10000):
    print '0x%04x,' % (i if i not in b2u else b2u[i]),
    if i % 10 == 9:
        print ''
print "};\n"

# u2b
u2b = open('uao250-u2b.big5.txt', 'r').readlines()
u2b = [line.strip().split(' ')
       for line in u2b
       if line.strip().startswith('0x')]
u2b = dict((int(u, 0), int(b, 0)) for (b, u) in u2b)

print "const uint16_t const u2b_table[0x10000] = {"
for i in range(0x10000):
    print '0x%04x,' % (i if i not in u2b else u2b[i]),
    if i % 10 == 9:
        print ''
print "};\n"

# ambiguous cjk width
ambcjk_data = open("ambcjk.big5.txt").readlines()
ambcjk = []
for entry in ambcjk_data:
    (a, b) = entry.strip().split()
    a = int(a, 0)
    b = int(b, 0)
    ambcjk = ambcjk + range(a, b+1)
print "const uint8_t const b2u_ambiguous_width[0x10000] = {"
for i in range(0, 0x10000):
    if i in b2u and b2u[i] in ambcjk:
        print "1,",
    else:
        print "0,",
    if i % 25 == 24:
        print ''

print "};\n"
