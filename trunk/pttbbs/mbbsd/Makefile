# $Id: Makefile,v 1.8 2002/06/04 13:08:33 in2 Exp $

BBSHOME?=$(HOME)
OSTYPE=FreeBSD

# FreeBSD
CFLAGS_FreeBSD=	-pipe -Wall -DHAVE_SETPROCTITLE -DBBSHOME='"$(BBSHOME)"' -DFreeBSD -I../include
LDFLAGS_FreeBSD=-pipe -Wall 
LIBS_FreeBSD=	-lutil -lkvm

# Linux
CFLAGS_linux=	-pipe -Wall -DHAVE_DES_CRYPT -DBBSHOME='"$(BBSHOME)"' -DLinux -I../include -s
LDFLAGS_linux=	-pipe -Wall 
LIBS_linux=	-lcrypt

CFLAGS=	$(CFLAGS_$(OSTYPE))
LDFLAGS=$(LDFLAGS_$(OSTYPE))
LIBS=	$(LIBS_$(OSTYPE))

.if defined(GDB) || defined(DEBUG)
CFLAGS+= -g
FDFLAGS+= -g
.else
CFLAGS+= -O
LDFLAGS+= -O
.endif

.if defined(DEBUG)
CFLAGS+= -DDEBUG
LDFLAGS+= -DDEBUG
.endif

.if defined(NO_FORK)
CFLAGS+= -DNO_FORK
.endif

CC=	gcc
PROG=	mbbsd
OBJS=	admin.o announce.o args.o bbcall.o bbs.o board.o cache.o cal.o card.o\
	chat.o chc_draw.o chc_net.o chc_play.o chc_rule.o chicken.o dark.o\
	dice.o edit.o friend.o gamble.o gomo.o gomo1.o guess.o indict.o io.o\
	kaede.o lovepaper.o mail.o mbbsd.o menu.o more.o name.o osdep.o\
	othello.o page.o read.o record.o register.o screen.o stuff.o\
	talk.o term.o topsong.o user.o vice.o vote.o xyz.o\
	voteboard.o syspost.o var.o descrypt.o toolkit.o passwd.o\
	calendar.o

.SUFFIXES: .c .o
.c.o:	../include/var.h
	$(CC) $(ADDFLAGS) $(CFLAGS) -c $*.c

all: $(PROG)

$(PROG): $(OBJS)
	$(CC) $(ADDFLAGS) $(LDFLAGS) -o $(PROG) $(OBJS) $(LIBS)

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
	mv $(BBSHOME)/bin/mbbsd $(BBSHOME)/bin/mbbsd.`date '+%m%d%H'`
	ln -sv $(BBSHOME)/bin/mbbsd.`date '+%m%d%H'` $(BBSHOME)/bin/mbbsd

clean:
	rm -f $(OBJS) $(PROG)
