# $Id$

SRCROOT=	../..
.include "$(SRCROOT)/pttbbs.mk"

PROG=	boardd
SRCS=	boardd.c server.c

CLEANFILES+=	*~
UTILDIR=	$(SRCROOT)/util
UTILOBJ=	$(UTILDIR)/util_var.o

LIBEVENT_CFLAGS!=	pkg-config --cflags libevent
LIBEVENT_LIBS_L!=	pkg-config --libs-only-L libevent
LIBEVENT_LIBS_l!=	pkg-config --libs-only-l libevent

CFLAGS+=	$(LIBEVENT_CFLAGS)
LDFLAGS+=	$(LIBEVENT_LIBS_L)

LDADD+= $(UTILOBJ) \
	$(SRCROOT)/common/bbs/libcmbbs.a \
	$(SRCROOT)/common/sys/libcmsys.a \
	$(SRCROOT)/common/osdep/libosdep.a \
	$(LIBEVENT_LIBS_l)

.include <bsd.prog.mk>
