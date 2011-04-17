# $Id$

SRCROOT=	../..
.include "$(SRCROOT)/pttbbs.mk"

SRCS:=	daemon.c file.c lock.c log.c net.c sort.c string.c time.c \
    crypt.c record.c vector.c telnet.c vbuf.c vtkbd.c utf8.c big5.c

LIB:=	cmsys

all: .depend

big5.c: big5data.zip big5_gen.py
	unzip big5data.zip
	./big5_gen.py > big5.c
	rm -f *.big5.txt

install:

.include <bsd.lib.mk>
