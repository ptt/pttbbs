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

SRCS+=	migrate_pttdb_to_file.c migrate_file_to_pttdb.c parse_legacy_file_comment_create_milli_timestamp_reset_karma.c

.endif

#######################################################################
# LIB
#######################################################################
LIB:=	cmmigrate_pttdb

install:

all:	$(SRCROOT)/include/var.h .depend

.include <bsd.lib.mk>
