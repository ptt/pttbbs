# $Id$
# 定義基本初值
BBSHOME?=	$(HOME)
BBSHOME?=	/home/bbs

SRCROOT?=	.

OSTYPE!=	uname

CC=		gcc
CCACHE!=	which ccache|sed -e 's/^.*\///'

PTT_CFLAGS:=	-Wall -pipe -DBBSHOME='"$(BBSHOME)"' -I$(SRCROOT)/include
PTT_LDFLAGS=
PTT_LDLIBS=	-lhz

# enable assert()
#PTT_CFLAGS+=	-DNDEBUG 

.if ${OSTYPE} == "FreeBSD"
# FreeBSD特有的環境
PTT_CFLAGS+=  -I/usr/local/include
PTT_LDFLAGS+= -L/usr/local/lib
PTT_LDLIBS+=   -lkvm -liconv
.endif

# 若有定義 PROFILING
.if defined(PROFILING)
PTT_CFLAGS+=	-pg
PTT_LDFLAGS+=	-pg
NO_OMITFP=	yes
NO_FORK=	yes
.endif

# 若有定義 DEBUG, 則在 CFLAGS內定義 DEBUG
.if defined(DEBUG)
GDB=		1
#CFLAGS+=	-DDEBUG
PTT_CFLAGS+=	-DDEBUG 
.endif

.if defined(GDB)
CFLAGS:=	-g -O0 $(PTT_CFLAGS)
LDFLAGS:=	-O0 $(PTT_LDFLAGS)
LDLIBS:=	$(PTT_LDLIBS)
.else
CFLAGS:=	-g -Os $(PTT_CFLAGS) $(EXT_CFLAGS)
LDFLAGS:=	-Os $(PTT_LDFLAGS)
LDLIBS:=	$(PTT_LDLIBS)

.if defined(OMITFP)
CFLAGS+=	-fomit-frame-pointer
.endif
.endif


# 若有定義 NO_FORK, 則在 CFLAGS內定義 NO_FORK
.if defined(NO_FORK)
CFLAGS+=	-DNO_FORK
.endif
