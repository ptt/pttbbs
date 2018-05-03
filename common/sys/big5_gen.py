#!/usr/bin/env python
# Usage: ./big5_gen.py > big5.c

from __future__ import print_function
import sys
import tarfile

BIG5_DATA = tarfile.open('big5data.tar.bz2')
B2U_FILE = BIG5_DATA.extractfile('uao250-b2u.big5.txt')
U2B_FILE = BIG5_DATA.extractfile('uao250-u2b.big5.txt')
AMBCJK_FILE = BIG5_DATA.extractfile('ambcjk.big5.txt')

# b2u
sys.stderr.write('Generating B2U data...\n')
b2u = B2U_FILE.readlines()
b2u = [line.strip().split(b' ')
       for line in b2u
       if line.strip().startswith(b'0x')]
b2u = dict((int(b, 0), int(u, 0)) for (b, u) in b2u)

print("""#include <stdint.h>
extern const uint16_t b2u_table[];
extern const uint16_t u2b_table[];
extern const uint8_t b2u_ambiguous_width[];
""")
print("const uint16_t b2u_table[0x10000] = {")
for i in range(0x10000):
    print( '0x%04x,' % (i if i not in b2u else b2u[i]), end=' ')
    if i % 10 == 9:
        print('')
print("};\n")

# u2b
sys.stderr.write('Generating U2B data...\n')
u2b = U2B_FILE.readlines()
u2b = [line.strip().split()
       for line in u2b
       if line.strip().startswith(b'0x')]
u2b = dict((int(u, 0), int(b, 0)) for (b, u) in u2b)

print("const uint16_t u2b_table[0x10000] = {")
for i in range(0x10000):
    print( '0x%04x,' % (i if i not in u2b else u2b[i]), end=' ')
    if i % 10 == 9:
        print('')
print("};\n")

# ambiguous cjk width
sys.stderr.write('Generating AMBCJK data...\n')
ambcjk_data = AMBCJK_FILE.readlines()
ambcjk = []
for entry in ambcjk_data:
    (a, b) = entry.strip().split()
    a = int(a, 0)
    b = int(b, 0)
    ambcjk = ambcjk + list( range(a, b+1) )
print("const uint8_t b2u_ambiguous_width[0x10000] = {")
for i in range(0, 0x10000):
    if i in b2u and b2u[i] in ambcjk:
        print("1,",end=' '),
    else:
        print("0,",end=' '),
    if i % 25 == 24:
        print('')

print("};\n")
