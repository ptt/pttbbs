# $Id$
# 定義基本初值
BBSHOME?=	$(HOME)
BBSHOME?=	/home/bbs
OSTYPE!=	uname
CC?=		gcc
CCACHE!=	which ccache|sed -e 's/^.*\///'
PTT_CFLAGS=	-Wall -pipe -DBBSHOME='"$(BBSHOME)"' -I../include
PTT_LDFLAGS=	-pipe -Wall -L/usr/local/lib
PTT_LIBS=	-lcrypt -lhz -liconv

# enable assert()
#PTT_CFLAGS+=	-DNDEBUG 

# FreeBSD特有的環境
CFLAGS_FreeBSD=	-DHAVE_SETPROCTITLE -DFreeBSD
LDFLAGS_FreeBSD=
LIBS_FreeBSD=	-lkvm

# Linux特有的環境
# CFLAGS_linux=   -DHAVE_DES_CRYPT -DLinux
CFLAGS_Linux=	
LDFLAGS_Linux=	-pipe -Wall 
LIBS_Linux=	

# CFLAGS, LDFLAGS, LIBS 加入 OS 相關參數
PTT_CFLAGS+=	$(CFLAGS_$(OSTYPE))
PTT_LDFLAGS+=	$(LDFLAGS_$(OSTYPE))
PTT_LIBS+=	$(LIBS_$(OSTYPE))

# 若有定義 GDB或 DEBUG, 則加入 -g , 否則用 -O
.if defined(GDB) || defined(DEBUG)
CFLAGS=		-g $(PTT_CFLAGS)
LDFLAGS=	-g $(PTT_LDFLAGS) $(PTT_LIBS)
.else
CFLAGS+=	-O2 -Os -fomit-frame-pointer -fstrength-reduce \
		-fthread-jumps -fexpensive-optimizations \
		$(PTT_CFLAGS)
LDFLAGS+=	-O2 $(PTT_LDFLAGS) $(PTT_LIBS)
.endif

# 若有定義 DEBUG, 則在 CFLAGS內定義 DEBUG
.if defined(DEBUG)
CFLAGS+=	-DDEBUG
.endif

# 若有定義 NO_FORK, 則在 CFLAGS內定義 NO_FORK
.if defined(NO_FORK)
CFLAGS+=	-DNO_FORK
.endif
