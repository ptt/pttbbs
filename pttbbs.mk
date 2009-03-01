# $Id$
# 定義基本初值
BBSHOME?=	$(HOME)
BBSHOME?=	/home/bbs

SRCROOT?=	.

OSTYPE!=	uname

CC:=		gcc
CXX:=		g++
CCACHE!=	which ccache|sed -e 's/^.*\///'
.if $(CCACHE)
CC:=		ccache $(CC)
CXX:=		ccache $(CXX)
.endif

PTT_CFLAGS:=	-Wall -pipe -DBBSHOME='"$(BBSHOME)"' -I$(SRCROOT)/include
PTT_CXXFLAGS:=	-Wall -pipe -DBBSHOME='"$(BBSHOME)"' -I$(SRCROOT)/include
PTT_LDFLAGS:=
PTT_LDLIBS:=	-lhz

# enable assert()
#PTT_CFLAGS+=	-DNDEBUG 

.if ${OSTYPE} == "FreeBSD"
# FreeBSD特有的環境
PTT_CFLAGS+=	-I/usr/local/include
PTT_CXXFLAGS+=	-I/usr/local/include
PTT_LDFLAGS+=	-L/usr/local/lib
PTT_LDLIBS+=	-lkvm -liconv
.endif

# 若有定義 PROFILING
.if defined(PROFILING)
PTT_CFLAGS+=	-pg
PTT_CXXFLAGS+=	-pg
PTT_LDFLAGS+=	-pg
NO_OMITFP=	yes
NO_FORK=	yes
.endif

# 若有定義 DEBUG, 則在 CFLAGS內定義 DEBUG
.if defined(DEBUG)
GDB=		1
#CFLAGS+=	-DDEBUG
PTT_CFLAGS+=	-DDEBUG 
PTT_CXXFLAGS+=	-DDEBUG 
.endif

.if defined(GDB)
CFLAGS:=	-g -O0 $(PTT_CFLAGS)
CXXFLAGS:=	-g -O0 $(PTT_CXXFLAGS)
LDFLAGS:=	-O0 $(PTT_LDFLAGS)
LDLIBS:=	$(PTT_LDLIBS)
.else
CFLAGS:=	-g -Os $(PTT_CFLAGS) $(EXT_CFLAGS)
CXXFLAGS:=	-g -Os $(PTT_CXXFLAGS) $(EXT_CXXFLAGS)
LDFLAGS:=	-Os $(PTT_LDFLAGS)
LDLIBS:=	$(PTT_LDLIBS)

.if defined(OMITFP)
CFLAGS+=	-fomit-frame-pointer
CXXFLAGS+=	-fomit-frame-pointer
.endif
.endif


# 若有定義 NO_FORK, 則在 CFLAGS內定義 NO_FORK
.if defined(NO_FORK)
CFLAGS+=	-DNO_FORK
CXXFLAGS+=	-DNO_FORK
.endif

######################################
# Settings for common libraries

# NetBSD pmake
MKLINT:=no
MKPROFILE:=no
MKPIC:=no
# Do not take warnings as errors
NOGCCERROR:=no

# FreeBSD make
WITHOUT_PROFILE:=yes

######################################

.MAIN: all

$(SRCROOT)/include/var.h:	$(SRCROOT)/mbbsd/var.c
	perl $(SRCROOT)/util/parsevar.pl < $(SRCROOT)/mbbsd/var.c > $(SRCROOT)/include/var.h

