# $Id$

SRCROOT=	../..
.include "$(SRCROOT)/pttbbs.mk"

PROG=	boardd
SRCS=	boardd.c server.cpp
MAN=

UTILDIR=	$(SRCROOT)/util
UTILOBJ=	$(UTILDIR)/util_var.o

COMMONFLAGS=
CXXFLAGS+=	-std=c++11 $(COMMONFLAGS)
CFLAGS+=	$(LIBEVENT_CFLAGS) $(COMMONFLAGS)
LDFLAGS+=	$(LIBEVENT_LIBS_L)

LDADD:=  $(UTILOBJ) \
	$(SRCROOT)/common/bbs/libcmbbs.a \
	$(SRCROOT)/common/sys/libcmsys.a \
	$(SRCROOT)/common/osdep/libosdep.a \
	$(LIBEVENT_LIBS_l) -levent_pthreads \
	-pthread -lstdc++ -lboost_system \
	$(LDADD)

.include <bsd.prog.mk>
