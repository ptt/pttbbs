# $Id$

SRCROOT=	..
.include "$(SRCROOT)/pttbbs.mk"

#######################################################################
# common modules
#######################################################################

PROG=	mbbsd
COREOBJS = bbs.o announce.o read.o board.o cache.o cal.o brc.o mail.o record.o fav.o
ACCOBJS  = user.o register.o passwd.o
NETOBJS  = mbbsd.o io.o term.o telnet.o
TALKOBJS = talk.o chat.o friend.o
UTILOBJS = stuff.o kaede.o convert.o name.o syspost.o
PAGEROBJS= more.o pmore.o
PLUGOBJS = lovepaper.o calendar.o topsong.o gamble.o vice.o angel.o
CHESSOBJS= chc.o chc_tab.o chess.o go.o gomo.o dark.o reversi.o
GAMEOBJS = card.o guess.o chicken.o othello.o
OBJS:=	admin.o assess.o edit.o menu.o xyz.o var.o visio.o \
	vote.o voteboard.o \
	$(COREOBJS)  $(ACCOBJS) $(NETOBJS) $(TALKOBJS) $(UTILOBJS) \
	$(PAGEROBJS) $(PLUGOBJS) \
	$(CHESSOBJS) $(GAMEOBJS)

#######################################################################
# conditional configurations and optional modules
#######################################################################

.if !defined(WITHOUT_BLOG) 	&& defined(WITH_BLOG)
CFLAGS+= -DBLOG
LDLIBS:= ${LDLIBS} ${:!mysql_config --libs!}
.endif

.if !defined(WITHOUT_LOG_CRAWLER) && defined(WITH_LOG_CRAWLER)
CFLAGS+= -DLOG_CRAWLER 
.endif

.if !defined(WITHOUT_EMAILDB)	&& defined(WITH_EMAILDB)
OBJS+=   emaildb.o
CFLAGS+= -DUSE_EMAILDB
LDLIBS+= -lsqlite3
.endif

.if !defined(WITHOUT_BBSLUA_USAGE) && defined(WITH_BBSLUA_USAGE)
CFLAGS+= -DBBSLUA_USAGE
.endif

.if !defined(WITHOUT_BBSLUA)	&& defined(WITH_BBSLUA)
OBJS+=	 bbslua.o bbsluaext.o
CFLAGS+= -DUSE_BBSLUA
# MODIFY THESE ENVIRONMENT SETTINGS TO FIT YOUR CONFIGURATION
.if $(OSTYPE)=="FreeBSD"
CFLAGS:= ${CFLAGS} ${:!pkg-config --cflags lua-5.1!}
LDLIBS:= ${LDLIBS} ${:!pkg-config --libs lua-5.1!}
.else
CFLAGS:= ${CFLAGS} ${:!pkg-config --cflags lua5.1!}
LDLIBS:= ${LDLIBS} ${:!pkg-config --libs lua5.1!}
.endif
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
DIETCC:=	diet -Os
.endif

# reduce .bss align overhead
.if !defined(DEBUG)
LDFLAGS+=-Wl,--sort-common
.endif

#.if defined(MERGEBBS)
#CFLAGS+= -DMERGEBBS
#OBJS+= ../upgrade/merge_sob.o
#.endif

LDLIBS+=$(SRCROOT)/common/bbs/libcmbbs.a \
	$(SRCROOT)/common/sys/libcmsys.a \
	$(SRCROOT)/common/osdep/libosdep.a

#######################################################################
# Make Rules
#######################################################################

.SUFFIXES: .c .o
.c.o:	$(SRCROOT)/include/var.h
	$(CC) $(CFLAGS) -c $*.c

all: $(PROG)

$(PROG): $(OBJS)
	sh $(SRCROOT)/util/newvers.sh
	$(CC) $(LDFLAGS) -o $(PROG) $(OBJS) $(LDLIBS) vers.c

$(SRCROOT)/include/banip.h: $(SRCROOT)/util/banip.pl
	perl $(SRCROOT)/util/banip.pl > $@

mbbsd.o: mbbsd.c $(SRCROOT)/include/var.h $(SRCROOT)/include/banip.h
	$(DIETCC) $(CC) $(CFLAGS) -c $<

initemaildb: emaildb.c
	$(CC) -DINIT_MAIN $(CFLAGS) $(LDFLAGS) -o initemaildb emaildb.c $(LDLIBS)

ctags:
	ctags *.c $(SRCROOT)/include/*.h $(SRCROOT)/common/sys/*.c $(SRCROOT)/common/bbs/*.c

test: $(PROG)
	killall -TERM testmbbsd || true
	sleep 1
	killall -9 testmbbsd || true
	cp mbbsd testmbbsd
	./testmbbsd -d -p9000
	rm -f testmbbsd

install: $(PROG)
	install -d $(BBSHOME)/bin/
	install -c -m 755 $(PROG) $(BBSHOME)/bin/
	mv -f $(BBSHOME)/bin/mbbsd $(BBSHOME)/bin/mbbsd.`date '+%m%d%H%M'`
	ln -sv $(BBSHOME)/bin/mbbsd.`date '+%m%d%H%M'` $(BBSHOME)/bin/mbbsd

clean:
	rm -f $(OBJS) $(PROG)
