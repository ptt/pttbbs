# $Id$

SRCROOT:=	../..
.include "$(SRCROOT)/pttbbs.mk"
.include "$(SRCROOT)/pttbbs_cond.mk"

#######################################################################
# MONGO
#######################################################################
.if $(MONGO_CLIENT_URL)
MONGO_CFLAGS:=	-I/usr/local/include/libmongoc-1.0 -I/usr/local/include/libbson-1.0
MONGO_LDFLAGS:=	-L/usr/local/lib
MONGO_LIBS:=	-L/usr/local/lib -lmongoc-1.0 -lbson-1.0 -lresolv
CFLAGS+=	${MONGO_CFLAGS}
LDFLAGS+=	${MONGO_LDFLAGS}
LDLIBS+=	${MONGO_LIBS}

SRCS+=	ptt_queue.c ptt_link_list.c pttlib_util.c

.endif

#######################################################################
# LIB
#######################################################################
LIB:=	cmpttlib

install:

all:	$(SRCROOT)/include/var.h .depend

.include <bsd.lib.mk>
