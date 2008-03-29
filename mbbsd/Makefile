# $Id$

SRCROOT=	..
.include "$(SRCROOT)/pttbbs.mk"

#######################################################################
# common modules
#######################################################################

PROG=	mbbsd
CHESSOBJS= chc.o chc_tab.o chess.o go.o gomo.o dark.o reversi.o
GAMEOBJS = card.o guess.o chicken.o othello.o
COREOBJS = bbs.o announce.o read.o board.o cache.o brc.o mail.o record.o fav.o
ACCOBJS  = user.o register.o passwd.o
TALKOBJS = talk.o chat.o friend.o
NETOBJS  = mbbsd.o io.o term.o
UTILOBJS = stuff.o kaede.o convert.o name.o
PLUGOBJS = lovepaper.o calendar.o topsong.o vice.o
OBJS=	admin.o assess.o cal.o edit.o menu.o more.o gamble.o \
	xyz.o syspost.o vote.o var.o voteboard.o \
	pmore.o telnet.o \
	$(COREOBJS) $(ACCOBJS)  $(NETOBJS)   $(TALKOBJS) $(UTILOBJS) \
	$(SYSOBJS)  $(PLUGOBJS) $(CHESSOBJS) $(GAMEOBJS)

#######################################################################
# conditional configurations and optional modules
#######################################################################

.if !defined(WITHOUT_BLOG) 	&& defined(WITH_BLOG)
CFLAGS+=  -DBLOG
LDFLAGS+= -L/usr/local/lib/mysql 
LIBS+=    -lmysqlclient
.endif

.if !defined(WITHOUT_LOG_CRAWLER) && defined(WITH_LOG_CRAWLER)
CFLAGS+= -DLOG_CRAWLER 
.endif

.if !defined(WITHOUT_EMAILDB)	&& defined(WITH_EMAILDB)
OBJS+=   emaildb.o
CFLAGS+= -DUSE_EMAILDB
LIBS+= 	 -lsqlite3
.endif

.if !defined(WITHOUT_BBSLUA_USAGE) && defined(WITH_BBSLUA_USAGE)
CFLAGS+= -DBBSLUA_USAGE
.endif

.if !defined(WITHOUT_BBSLUA)	&& defined(WITH_BBSLUA)
OBJS+=	 bbslua.o bbsluaext.o
CFLAGS+= -DUSE_BBSLUA
# MODIFY THESE ENVIRONMENT SETTINGS TO FIT YOUR CONFIGURATION
CFLAGS+= -I/usr/include/lua5.1 
CFLAGS_FreeBSD += -I/usr/local/include/lua51
LDFLAGS_FreeBSD+= -L/usr/local/lib/lua51
# modify the lib name below to fit your configuration
# usually you'd try "-llua" instead of "-llua5.1".
LIBS+= -llua5.1 -lm
#LIBS+= -llua -lm
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

#.if defined(MERGEBBS)
#CFLAGS+= -DMERGEBBS
#OBJS+= ../upgrade/merge_sob.o
#.endif

LIBS+=	$(SRCROOT)/common/bbs/libcmbbs.a \
	$(SRCROOT)/common/sys/libcmsys.a

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

initemaildb: emaildb.c
	$(CC) -DINIT_MAIN $(CFLAGS) $(LDFLAGS) -o initemaildb emaildb.c $(LIBS)

ctags:
	ctags *.c ../include/*.h ../common/sys/*.c ../common/bbs/*.c

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
