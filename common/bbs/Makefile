# For NetBSD pmake
MKLINT:=no
MKPROFILE:=no
MKPIC:=no

# For FreeBSD make
WITHOUT_PROFILE:=yes

SRCROOT:=	../..
.include "$(SRCROOT)/pttbbs.mk"

SRCS:=	log.c money.c names.c path.c time.c string.c fhdr_stamp.c
LIB:=	cmbbs

install:

all:	$(SRCROOT)/include/var.h .depend

.include <bsd.lib.mk>
