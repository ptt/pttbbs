# $Id$

SRCROOT:=	../..
.include "$(SRCROOT)/pttbbs.mk"
.include "$(SRCROOT)/pttbbs_cond.mk"

.if $(MONGO_CLIENT_URL)
MONGO_CFLAGS:=	-I/usr/local/include/libmongoc-1.0 -I/usr/local/include/libbson-1.0
MONGO_LDFLAGS:=	-L/usr/local/lib
MONGO_LIBS:=	-L/usr/local/lib -lmongoc-1.0 -lbson-1.0 -lresolv
CFLAGS+=	${MONGO_CFLAGS}
LDFLAGS+=	${MONGO_LDFLAGS}
LDLIBS+=	${MONGO_LIBS}

SRCS+=	pttui_thread_lock.c pttui_thread.c pttui_var.c pttui_buffer.c pttui_resource_info.c pttui_resource_dict.c pttui_lua_bbs.c pttui_util.c pttui_const.c vedit3.c vedit3_action.c pttui_id_comment_id_dict.c

.endif

#######################################################################
# LIB
#######################################################################
LIB:=	cmpttui

install:

all:	$(SRCROOT)/include/var.h .depend

.include <bsd.lib.mk>
