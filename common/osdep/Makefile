# For NetBSD pmake
MKLINT:=no
MKPROFILE:=no
MKPIC:=no

# For FreeBSD make
WITHOUT_PROFILE:=yes

SRCROOT=	../..
.include "$(SRCROOT)/pttbbs.mk"

SRCS:=	cpuload.c proctitle.c strlcat.c strlcpy.c
LIB:=	osdep

all: .depend

install:

.include <bsd.lib.mk>
