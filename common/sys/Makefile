# For NetBSD pmake
MKLINT:=no
MKPROFILE:=no
MKPIC:=no

# For FreeBSD make
WITHOUT_PROFILE:=yes

SRCROOT=	../..
.include "$(SRCROOT)/pttbbs.mk"

SRCS:=	daemon.c file.c lock.c log.c net.c sort.c string.c time.c crypt.c record.c vector.c
LIB:=	cmsys

all: .depend

install:

.include <bsd.lib.mk>
