# $Id$

SRCROOT=	../..
.include "$(SRCROOT)/pttbbs.mk"

SRCS:=	daemon.c file.c lock.c log.c net.c sort.c string.c time.c \
	crypt.c record.c vector.c telnet.c vbuf.c vtkbd.c \
	utf8.c big5.c buffer.c thttp.c

LIB:=	cmsys

all: .depend

big5.c: big5data.tar.bz2 big5_gen.py
	tar jxf big5data.tar.bz2
	./big5_gen.py > big5.c
	tar jtf big5data.tar.bz2 | xargs rm

install:

.include <bsd.lib.mk>
