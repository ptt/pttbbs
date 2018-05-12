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

SRCS+=	pttdb_util.c util_db_mongo.c pttdb_uuid.c pttdb_dict_bson_by_uu.c pttdb_dict_idx_by_uu.c pttdb_main.c pttdb_content_block.c pttdb_comment.c pttdb_comment_const.c pttdb_comment_const_utf8.c pttdb_comment_reply.c pttdb_file_info.c pttdb_file.c

.endif

#######################################################################
# LIB
#######################################################################
LIB:=	cmpttdb

install:

all:	$(SRCROOT)/include/var.h .depend

.include <bsd.lib.mk>
