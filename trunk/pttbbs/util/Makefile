# $Id: Makefile,v 1.28 2003/04/05 13:09:03 in2 Exp $

# 訂義基本初值
BBSHOME?=	$(HOME)
BBSHOME?=	/home/bbs
OSTYPE?=	FreeBSD
CC?=		gcc
CFLAGS+=	-Wall -pipe -DBBSHOME='"$(BBSHOME)"' -I../include
LDFLAGS+=	-pipe -Wall
LIBS+=		-lcrypt

# FreeBSD特有的環境
CFLAGS_FreeBSD= -DHAVE_SETPROCTITLE -DFreeBSD
LDFLAGS_FreeBSD=
LIBS_FreeBSD=   -lkvm

# Linux特有的環境
CFLAGS_linux=   -DHAVE_DES_CRYPT -DLinux
LDFLAGS_linux=  -pipe -Wall
LIBS_linux=	-lresolv

# CFLAGS, LDFLAGS, LIBS 加入 OS 相關參數
CFLAGS+=	$(CFLAGS_$(OSTYPE))
LDFLAGS+=	$(LDFLAGS_$(OSTYPE))
LIBS+=		$(LIBS_$(OSTYPE))

OBJS=   util_cache.o util_record.o util_passwd.o 

CPROGS=	bbsmail BM_money post account birth deluserfile expire mandex\
	horoscope openvice parse_news openticket topusr yearsold uhash_loader\
	poststat showboard antispam countalldice webgrep bbsrf\
	initbbs outmail xchatd userlist tunepasswd buildir reaper shmsweep\
	merge_passwd merge_board inndBM buildAnnounce rmuid \
	toplazyBM jungo	toplazyBBM shmctl indexuser 

PROGS=	$(CPROGS) BM_money.sh backpasswd.sh mailog.sh opendice.sh\
	openticket.sh stock.sh topsong.sh weather.sh stock.perl weather.perl\
	toplazyBM.sh toplazyBBM.sh dailybackup.pl tarqueue.pl waterball.pl \
	filtermail.pl getbackup.pl udnnews.pl rebuildaloha.pl railway_wrapper.pl

.if ${OSTYPE} == "FreeBSD"
CPROGS += diskstat
.endif

all: $(PROGS)

bbsmail: bbsmail.c $(OBJS)
	$(CC) $(CFLAGS) -o $@ $@.c $(OBJS)

BM_money: BM_money.c $(OBJS)
	$(CC) $(CFLAGS) -o $@ $@.c $(OBJS)

post: post.c $(OBJS)
	$(CC) $(CFLAGS) -o $@ $@.c  $(OBJS)

poststat: poststat.c $(OBJS)
	$(CC) $(CFLAGS) -o $@ $@.c  $(OBJS)

jungo: jungo.c $(OBJS)
	$(CC) $(CFLAGS) -o $@ $@.c  $(OBJS) 

account: account.c  $(OBJS)
	$(CC) $(CFLAGS) -o $@ $@.c  $(OBJS)

birth: birth.c  $(OBJS)
	$(CC) $(CFLAGS) -o $@ $@.c  $(OBJS)

deluserfile: deluserfile.c  $(OBJS)
	$(CC) $(CFLAGS) -o $@ $@.c  $(OBJS)

expire: expire.c  $(OBJS)
	$(CC) $(CFLAGS) -o $@ $@.c $(OBJS)

mandex: mandex.c $(OBJS)
	$(CC) $(CFLAGS) -o $@ $@.c  $(OBJS)

rmuid: rmuid.c  $(OBJS)
	$(CC) $(CFLAGS) -o $@ $@.c   $(OBJS)

horoscope: horoscope.c  $(OBJS)
	$(CC) $(CFLAGS) -o $@ $@.c  $(OBJS)

openvice: openvice.c  $(OBJS)
	$(CC) $(CFLAGS) -o $@ $@.c  $(OBJS)

parse_news: parse_news.c  $(OBJS)
	$(CC) $(CFLAGS) -o $@ $@.c  $(OBJS)

openticket: openticket.c  $(OBJS)
	$(CC) $(CFLAGS) -o $@ $@.c  $(OBJS)

topusr: topusr.c  $(OBJS)
	$(CC) $(CFLAGS) -o $@ $@.c $(OBJS)

indexuser: indexuser.c  $(OBJS)
	$(CC) $(CFLAGS) -o $@ $@.c $(OBJS)

yearsold: yearsold.c $(OBJS)
	$(CC) $(CFLAGS) -o $@ $@.c  $(OBJS)

xchatd: xchatd.c  $(OBJS) descrypt.c
	$(CC) $(CFLAGS) -o $@ $@.c  $(OBJS) descrypt.c $(LIBCHAT)

toplazyBM: toplazyBM.c  $(OBJS)
	$(CC) $(CFLAGS) -o $@ $@.c  $(OBJS)

toplazyBBM: toplazyBBM.c  $(OBJS)
	$(CC) $(CFLAGS) -o $@ $@.c  $(OBJS)

smtest:	smtest.c  $(OBJS)
	$(CC) $(CFLAGS) -o $@ $@.c  $(OBJS)

reaper: reaper.c  $(OBJS)
	$(CC) $(CFLAGS) -o $@ $@.c  $(OBJS)

buildAnnounce: buildAnnounce.c $(OBJS)
	$(CC) $(CFLAGS) -o $@ $@.c   $(OBJS)

inndBM: inndBM.c $(OBJS)
	$(CC) $(CFLAGS) -o $@ $@.c  $(OBJS) 

shmsweep: shmsweep.c
	$(CC) $(CFLAGS) -o $@ $@.c

uhash_loader: uhash_loader.c
	$(CC) $(CFLAGS) -o $@ $@.c

showboard: showboard.c
	$(CC) $(CFLAGS) -o $@ $@.c

antispam: antispam.c
	$(CC) $(CFLAGS) -o $@ $@.c

countalldice: countalldice.c
	$(CC) $(CFLAGS) -o $@ $@.c

webgrep: webgrep.c
	$(CC) $(CFLAGS) -o $@ $@.c

bbsrf: bbsrf.c
	$(CC) $(CFLAGS) -o $@ $@.c

initbbs: initbbs.c
	$(CC) $(CFLAGS) -o $@ $@.c

outmail: outmail.c
	$(CC) $(CFLAGS) -o $@ $@.c $(LIBMAIL)

userlist: userlist.c
	$(CC) $(CFLAGS) -o $@ $@.c

tunepasswd: tunepasswd.c
	$(CC) $(CFLAGS) -o $@ $@.c

buildir: buildir.c
	$(CC) $(CFLAGS) -o $@ $@.c

merge_passwd: merge_passwd.c
	$(CC) $(CFLAGS) -o $@ $@.c

merge_board : merge_board.c
	$(CC) $(CFLAGS) -o $@ $@.c

bbsctl:	bbsctl.c
	$(CC) $(CFLAGS) -o $@ $@.c

shmctl:	shmctl.c $(OBJS)
	$(CC) $(CFLAGS) -o $@ $@.c $(OBJS)

diskstat: diskstat.c
	$(CC) $(CFLAGS) -o diskstat diskstat.c -ldevstat -lkvm

installdiskstat: diskstat
	cp -f diskstat /usr/local/bin/
	chgrp kmem /usr/local/bin/diskstat
	chmod 2755 /usr/local/bin/diskstat

install: $(PROGS)
	install -d $(BBSHOME)/bin/
	install -c -m 755 $(PROGS) $(BBSHOME)/bin/
	chmod 4755 $(BBSHOME)/bin/post
	#mv $(BBSHOME)/bin/bbsmail $(BBSHOME)/bin/realbbsmail
	#ln -s $(BBSHOME)/bin/filtermail.pl $(BBSHOME)/bin/bbsmail

installbbsctl: bbsctl
	rm -f /home/bbs/bin/bbsctl
	cp /home/bbs/pttbbs/util/bbsctl /home/bbs/bin/bbsctl
	chown root /home/bbs/bin/bbsctl
	chmod 4755 /home/bbs/bin/bbsctl

clean:
	rm -f *.o $(CPROGS)
