# $Id: Makefile,v 1.24 2003/05/01 08:19:23 in2 Exp $

# 訂義基本初值
BBSHOME?=	$(HOME)
BBSHOME?=	/home/bbs
OSTYPE?=	FreeBSD
CC?=		gcc
PTT_CFLAGS=	-Wall -pipe -DBBSHOME='"$(BBSHOME)"' -I../include
PTT_LDFLAGS=	-pipe -Wall
PTT_LIBS=	-lcrypt

# 在 CFLAGS內加入定義 COMPILE_TIME
PTT_CFLAGS+=	"-DCOMPILE_TIME=\"`date`\""

# 稍後再 enable assert()
PTT_CFLAGS+=	-DNDEBUG 

# FreeBSD特有的環境
CFLAGS_FreeBSD=	-DHAVE_SETPROCTITLE -DFreeBSD
LDFLAGS_FreeBSD=
LIBS_FreeBSD=	-lkvm

# Linux特有的環境
CFLAGS_linux=	-DHAVE_DES_CRYPT -DLinux
LDFLAGS_linux=	-pipe -Wall 
LIBS_linux=	

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

PROG=	mbbsd
OBJS=	admin.o announce.o args.o bbs.o board.o cache.o cal.o card.o\
	chat.o chc_draw.o chc_net.o chc_play.o chc_rule.o chicken.o dark.o\
	edit.o friend.o gamble.o gomo.o gomo1.o guess.o indict.o io.o\
	kaede.o lovepaper.o mail.o mbbsd.o menu.o more.o name.o osdep.o\
	othello.o page.o read.o record.o register.o screen.o stuff.o\
	talk.o term.o topsong.o user.o vice.o vote.o xyz.o\
	voteboard.o syspost.o var.o toolkit.o passwd.o\
	calendar.o

.SUFFIXES: .c .o
.c.o:	../include/var.h
	$(CC) $(CFLAGS) -c $*.c

all: $(PROG)

$(PROG): $(OBJS)
	$(CC) $(LDFLAGS) -o $(PROG) $(OBJS) $(LIBS) $(EXT_LIBS)

../include/var.h:	var.c
	perl ../util/parsevar.pl < var.c > ../include/var.h

test: $(PROG)
	killall -9 testmbbsd || true
	cp mbbsd testmbbsd
	./testmbbsd 9000
	rm -f testmbbsd

install: $(PROG)
	install -d $(BBSHOME)/bin/
	install -c -m 755 $(PROG) $(BBSHOME)/bin/
	mv -f $(BBSHOME)/bin/mbbsd $(BBSHOME)/bin/mbbsd.`date '+%m%d%H'`
	ln -sv $(BBSHOME)/bin/mbbsd.`date '+%m%d%H'` $(BBSHOME)/bin/mbbsd

clean:
	rm -f $(OBJS) $(PROG)
