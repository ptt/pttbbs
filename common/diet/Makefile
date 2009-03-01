# $Id$

# For NetBSD pmake
MKLINT:=no
MKPROFILE:=no
MKPIC:=no
NOGCCERROR:=yes

# For FreeBSD make
WITHOUT_PROFILE:=yes

SRCROOT:=	../..
.include "$(SRCROOT)/pttbbs.mk"

SRCS:=	alloc.c random.c time.c
LIB:=	cmdiet

install:

.include <bsd.lib.mk>
