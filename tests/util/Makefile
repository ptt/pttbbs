# $Id$

TESTROOT:=	..

SRCROOT:=	../..
.include "$(SRCROOT)/pttbbs.mk"
.include "$(SRCROOT)/pttbbs_cond.mk"

# MONGO
.if $(MONGO_CLIENT_URL)
MONGO_CFLAGS:=	-I/usr/local/include/libmongoc-1.0 -I/usr/local/include/libbson-1.0
CXXFLAGS+=	${MONGO_CFLAGS}
CFLAGS+=	${MONGO_CFLAGS}

SRCS+=	test_util_db.c
.endif

CFLAGS+=	-I$(TESTROOT)/include
CXXFLAGS+=	-I$(TESTROOT)/include

# LIB
LIB:=	test_util

install:

all:	$(SRCROOT)/include/var.h .depend

.include <bsd.lib.mk>
