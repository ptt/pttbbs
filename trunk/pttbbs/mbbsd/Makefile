# $Id: Makefile,v 1.4 2002/03/29 13:45:28 ptt Exp $

BBSHOME?=$(HOME)
OSTYPE=FreeBSD

# FreeBSD
CFLAGS_FreeBSD=	-pipe -Wall -g -O3 -DHAVE_SETPROCTITLE -DBBSHOME='"$(BBSHOME)"' -DFreeBSD -I../include
LDFLAGS_FreeBSD=-pipe -Wall -g -O3
LIBS_FreeBSD=	-lutil -lkvm

# Linux
CFLAGS_linux=	-pipe -Wall -g -O3 -DHAVE_DES_CRYPT -DBBSHOME='"$(BBSHOME)"' -DLinux -I../include -s
LDFLAGS_linux=	-pipe -Wall -g -O3
LIBS_linux=	-lcrypt

CFLAGS=	$(CFLAGS_$(OSTYPE))
LDFLAGS=$(LDFLAGS_$(OSTYPE))
LIBS=	$(LIBS_$(OSTYPE))

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
.c.o:
	$(CC) $(CFLAGS) -c $*.c

all: $(PROG)

$(PROG): $(OBJS)
	$(CC) $(LDFLAGS) -o $(PROG) $(OBJS) $(LIBS)

test: $(PROG)
	killall -9 testmbbsd || true
	cp mbbsd testmbbsd
	./testmbbsd 9000
	rm -f testmbbsd

install: $(PROG)
	install -d $(BBSHOME)/bin/
	install -c -m 755 $(PROG) $(BBSHOME)/bin/

clean:
	rm -f $(OBJS) $(PROG)
