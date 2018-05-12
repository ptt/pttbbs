# $Id$

SRCROOT:=	../..
.include "$(SRCROOT)/pttbbs.mk"
.include "$(SRCROOT)/pttbbs_cond.mk"

.if $(MONGO_CLIENT_URL)

SRCS+=	util_timestamp.c

.endif

#######################################################################
# LIB
#######################################################################
LIB:=	cmutil_time

install:

all:	$(SRCROOT)/include/var.h .depend

.include <bsd.lib.mk>
