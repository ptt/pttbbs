# $Id$

SRCROOT=	../..
.include "$(SRCROOT)/pttbbs.mk"

PROG=	boardd

SRCS=	boardd.c

UTILDIR=	$(SRCROOT)/util
UTILOBJ=	$(UTILDIR)/util_var.o

LDADD+= $(UTILOBJ) \
	$(SRCROOT)/common/bbs/libcmbbs.a \
	$(SRCROOT)/common/sys/libcmsys.a \
	$(SRCROOT)/common/osdep/libosdep.a \
	-levent

.include <bsd.prog.mk>
