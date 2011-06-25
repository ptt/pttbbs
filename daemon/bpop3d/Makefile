# $Id$

SRCROOT=	../..
.include "$(SRCROOT)/pttbbs.mk"

PROG=	bpop3d
SRCS=	bpop3d.c server.c
MAN=

UTILDIR=	$(SRCROOT)/util
UTILOBJ=	$(UTILDIR)/util_var.o

CFLAGS+=	$(LIBEVENT_CFLAGS)
LDFLAGS+=	$(LIBEVENT_LIBS_L)

LDADD+= $(UTILOBJ) \
	$(SRCROOT)/common/bbs/libcmbbs.a \
	$(SRCROOT)/common/sys/libcmsys.a \
	$(SRCROOT)/common/osdep/libosdep.a \
	$(LIBEVENT_LIBS_l) -lcrypt

.include <bsd.prog.mk>
