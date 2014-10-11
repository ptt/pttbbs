# $Id$

SRCROOT=	../..
.include "$(SRCROOT)/pttbbs.mk"

PROG=	boardd
SRCS=	boardd.c server.c threadpool.cpp
MAN=

UTILDIR=	$(SRCROOT)/util
UTILOBJ=	$(UTILDIR)/util_var.o

CXXFLAGS+=	-std=c++11
CFLAGS+=	$(LIBEVENT_CFLAGS) -DSERVER_USE_PTHREADS
LDFLAGS+=	$(LIBEVENT_LIBS_L)

LDADD:=  $(UTILOBJ) \
	$(SRCROOT)/common/bbs/libcmbbs.a \
	$(SRCROOT)/common/sys/libcmsys.a \
	$(SRCROOT)/common/osdep/libosdep.a \
	$(LIBEVENT_LIBS_l) -levent_pthreads \
	-lpthread -lstdc++ \
	$(LDADD)

.include <bsd.prog.mk>
