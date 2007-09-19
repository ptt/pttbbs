# $Id$
# 定義基本初值
BBSHOME?=	$(HOME)
BBSHOME?=	/home/bbs

SRCROOT?=	.

OS!=		uname
OS_MAJOR_VER!=	uname -r|cut -d . -f 1
OS_MINOR_VER!=	uname -r|cut -d . -f 2
OSTYPE?=	$(OS)

CC=		gcc
CCACHE!=	which ccache|sed -e 's/^.*\///'
PTT_CFLAGS=	-Wall -pipe -DBBSHOME='"$(BBSHOME)"' -I$(SRCROOT)/include
PTT_LDFLAGS=	-L/usr/local/lib
PTT_LIBS=	-lhz

# enable assert()
#PTT_CFLAGS+=	-DNDEBUG 

# FreeBSD特有的環境
CFLAGS_FreeBSD=	-I/usr/local/include
LDFLAGS_FreeBSD=
LIBS_FreeBSD=	-lkvm -liconv

# Linux特有的環境
CFLAGS_Linux=	
LDFLAGS_Linux=	
LIBS_Linux=	

# SunOS特有的環境
CFLAGS_Solaris=	-DSolaris -I/usr/local/include 
LDFLAGS_Solaris= -L/usr/local/lib -L/usr/lib
LIBS_Solaris=	-lnsl -lsocket -liconv -lkstat

OS_FLAGS=	-D__OS_MAJOR_VERSION__="$(OS_MAJOR_VER)" \
		-D__OS_MINOR_VERSION__="$(OS_MINOR_VER)"

# CFLAGS, LDFLAGS, LIBS 加入 OS 相關參數
PTT_CFLAGS+=	$(CFLAGS_$(OSTYPE)) $(OS_FLAGS)
PTT_LDFLAGS+=	$(LDFLAGS_$(OSTYPE))
PTT_LIBS+=	$(LIBS_$(OSTYPE))


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

.if defined(USE_ICC)
CC=		icc
CFLAGS=		$(PTT_CFLAGS) -O1 -tpp6 -mcpu=pentiumpro -march=pentiumiii \
		-ip -ipo
LDFLAGS+=	-O1 -tpp6 -mcpu=pentiumpro -march=pentiumiii -ip -ipo \
		$(PTT_LDFLAGS) $(PTT_LIBS)
.elif defined(GDB)
CFLAGS=		-g -O0 $(PTT_CFLAGS)
LDFLAGS=	-O0 $(PTT_LDFLAGS) $(PTT_LIBS)
.else
CFLAGS+=	-g -Os $(PTT_CFLAGS) $(EXT_CFLAGS)
LDFLAGS+=	-Os $(PTT_LDFLAGS) $(PTT_LIBS)

.if defined(OMITFP)
CFLAGS+=	-fomit-frame-pointer
.endif
.endif


# 若有定義 NO_FORK, 則在 CFLAGS內定義 NO_FORK
.if defined(NO_FORK)
CFLAGS+=	-DNO_FORK
.endif
