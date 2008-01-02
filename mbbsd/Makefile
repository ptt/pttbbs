# $Id$

SRCROOT=	..
.include "$(SRCROOT)/pttbbs.mk"

#######################################################################
# common modules
#######################################################################

PROG=	mbbsd
CHESSOBJS= chc.o chc_tab.o chess.o go.o gomo.o dark.o reversi.o othello.o
GAMEOBJS = card.o guess.o chicken.o
SYSOBJS  = args.o crypt.o osdep.o
OBJS=	admin.o announce.o assess.o bbs.o board.o cache.o cal.o\
	chat.o convert.o edit.o fav.o friend.o gamble.o\
	indict.o io.o kaede.o lovepaper.o mail.o mbbsd.o menu.o\
	more.o name.o read.o record.o register.o\
	stuff.o talk.o term.o topsong.o user.o brc.o vice.o vote.o\
	xyz.o voteboard.o syspost.o var.o passwd.o calendar.o file.o \
	pmore.o telnet.o \
	$(SYSOBJS) $(CHESSOBJS) $(GAMEOBJS)

#######################################################################
# conditional configurations and optional modules
#######################################################################

.if !defined(WITHOUT_BLOG) 	&& defined(WITH_BLOG)
CFLAGS+=  -DBLOG
LDFLAGS+= -L/usr/local/lib/mysql -lmysqlclient
.endif

.if !defined(WITHOUT_EMAILDB)	&& defined(WITH_EMAILDB)
OBJS+=   emaildb.o
CFLAGS+= -DUSE_EMAILDB
LDFLAGS+= -lsqlite3
.endif

.if !defined(WITHOUT_PFTERM)	&& defined(WITH_PFTERM)
OBJS+=	pfterm.o
CFLAGS+=  -DUSE_PFTERM
#CFLAGS+= -DDBG_OUTRPT
.else
OBJS+=	screen.o
.endif

#######################################################################
# special library (DIET) configuration
#######################################################################

.if defined(DIET)
OBJS+=	random.o time.o alloc.o
DIETCC=	diet -Os
.endif
#CFLAGS+=-g
#CFLAGS+=-std=c99

# reduce .bss align overhead
.if !defined(DEBUG)
LDFLAGS+=-Wl,--sort-common
.endif

.if defined(MERGEBBS)
CFLAGS+= -DMERGEBBS
OBJS+= merge.o
.endif
LIBS+=	$(SRCROOT)/src/libbbsutil/libbbsutil.a \
	$(SRCROOT)/src/libbbs/libbbs.a

#######################################################################
# Make Rules
#######################################################################

.SUFFIXES: .c .o
.c.o:	$(SRCROOT)/include/var.h
	$(CCACHE) $(DIETCC) $(CC) $(CFLAGS) -c $*.c

all: $(PROG)

$(PROG): $(OBJS)
	sh $(SRCROOT)/util/newvers.sh
	$(DIETCC) $(CC) $(LDFLAGS) -o $(PROG) $(OBJS) $(LIBS) $(EXT_LIBS) vers.c

$(SRCROOT)/include/var.h:	var.c
	perl $(SRCROOT)/util/parsevar.pl < var.c > $(SRCROOT)/include/var.h

$(SRCROOT)/include/banip.h: $(SRCROOT)/util/banip.pl
	perl $(SRCROOT)/util/banip.pl > $@

mbbsd.o: mbbsd.c $(SRCROOT)/include/var.h $(SRCROOT)/include/banip.h
	$(CCACHE) $(DIETCC) $(CC) $(CFLAGS) -c $<

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
