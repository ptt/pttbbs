# $Id$

.include "../pttbbs.mk"

.if defined(WITH_BLOG)
CFLAGS+=  -DBLOG
LDFLAGS+= -L/usr/local/lib/mysql -lmysqlclient
.endif


PROG=	mbbsd
OBJS=	admin.o announce.o args.o assess.o bbs.o board.o cache.o cal.o card.o\
	chat.o chc.o chc_tab.o chicken.o convert.o crypt.o dark.o edit.o fav.o friend.o gamble.o\
	gomo.o guess.o indict.o io.o kaede.o lovepaper.o mail.o mbbsd.o menu.o\
	more.o name.o osdep.o othello.o read.o record.o register.o\
	screen.o stuff.o talk.o term.o topsong.o user.o brc.o vice.o vote.o\
	xyz.o voteboard.o syspost.o var.o passwd.o calendar.o go.o file.o \
	pmore.o chess.o

.if defined(DIET)
OBJS+=	random.o time.o
DIETCC=	diet -Os
.endif
CFLAGS+=-std=c99

# reduce .bss align overhead
.if !defined(DEBUG)
LDFLAGS+=-Wl,--sort-common
.endif

.if defined(MERGEBBS)
CFLAGS+= -DMERGEBBS
OBJS+= merge.o
.endif

.SUFFIXES: .c .o
.c.o:	../include/var.h
	$(CCACHE) $(DIETCC) $(CC) $(CFLAGS) -c $*.c

all: $(PROG)

$(PROG): $(OBJS)
	sh ../util/newvers.sh
	$(DIETCC) $(CC) $(LDFLAGS) -o $(PROG) $(OBJS) $(LIBS) $(EXT_LIBS) vers.c

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
	mv -f $(BBSHOME)/bin/mbbsd $(BBSHOME)/bin/mbbsd.`date '+%m%d%H%M'`
	ln -sv $(BBSHOME)/bin/mbbsd.`date '+%m%d%H%M'` $(BBSHOME)/bin/mbbsd

clean:
	rm -f $(OBJS) $(PROG)
