MKLINT:=no
MKPROFILE:=no
MKPIC:=no

SRCROOT:=	../..
.include "$(SRCROOT)/pttbbs.mk"

SRCS:=	log.c file.c money.c names.c path.c time.c string.c fhdr_stamp.c
LIB:=	cmbbs

install:

all:	$(SRCROOT)/include/var.h .depend

.include <bsd.lib.mk>
