# $Id: Makefile,v 1.29 2003/05/15 08:20:05 in2 Exp $

.include "../pttbbs.mk"

UTIL_OBJS=	\
	util_cache.o util_record.o util_passwd.o 

CPROG_WITH_UTIL= \
	bbsmail		BM_money	post		poststat	\
	jungo		account		birth		deluserfile	\
	expire		mandex		rmuid		horoscope	\
	openvice	parse_news	openticket	topusr		\
	indexuser	yearsold	toplazyBM	toplazyBBM	\
	reaper		buildAnnounce	inndBM		shmctl

CPROG_WITHOUT_UTIL= \
	shmsweep	uhash_loader	showboard	antispam	\
	countalldice	webgrep		bbsrf		initbbs		\
	userlist	tunepasswd	buildir		merge_passwd	\
	merge_board	xchatd		outmail

PROGS=	${UTIL_OBJS}	${CPROG_WITH_UTIL}	${CPROG_WITHOUT_UTIL}	\
	BM_money.sh	backpasswd.sh	mailog.sh	opendice.sh	\
	openticket.sh	stock.sh	topsong.sh	weather.sh	\
	stock.perl	weather.perl	toplazyBM.sh	toplazyBBM.sh	\
	dailybackup.pl	tarqueue.pl	waterball.pl	filtermail.pl	\
	getbackup.pl	udnnews.pl	rebuildaloha.pl	railway_wrapper.pl

all: ${CPROG_WITH_UTIL} ${CPROG_WITHOUT_UTIL} ${PROGS}

.for fn in ${CPROG_WITH_UTIL}
${fn}:	${fn}.c ${UTIL_OBJS}
	${CC} ${CFLAGS} -o ${fn} ${fn}.c ${UTIL_OBJS}
.endfor

xchatd: xchatd.c  $(OBJS) descrypt.c
	$(CC) $(CFLAGS) -o $@ $@.c  $(OBJS) descrypt.c $(LIBCHAT)

outmail: outmail.c
	$(CC) $(CFLAGS) -o $@ $@.c $(LIBMAIL)

install: $(PROGS)
	install -d $(BBSHOME)/bin/
	install -c -m 755 $(PROGS) $(BBSHOME)/bin/
	chmod 4755 $(BBSHOME)/bin/post

clean:
	rm -f *.o $(CPROGS) $(CPROG_WITH_UTIL) $(CPROG_WITHOUT_UTIL)


installfiltermail:
	mv $(BBSHOME)/bin/bbsmail $(BBSHOME)/bin/realbbsmail
	ln -s $(BBSHOME)/bin/filtermail.pl $(BBSHOME)/bin/bbsmail

# for diskstat(FreeBSD 4.x only) .
# diskstat should be compiled with bbs and installed with root
diskstat: diskstat.c
	$(CC) $(CFLAGS) -o diskstat diskstat.c -ldevstat -lkvm

installdiskstat: diskstat
	cp -f diskstat /usr/local/bin/
	chgrp kmem /usr/local/bin/diskstat
	chmod 2755 /usr/local/bin/diskstat

# for bbsctl. bbsctl should be compiled with bbs and installed with root
bbsctl:	bbsctl.c
	$(CC) $(CFLAGS) -o $@ $@.c

installbbsctl: bbsctl
	rm -f /home/bbs/bin/bbsctl
	cp /home/bbs/pttbbs/util/bbsctl /home/bbs/bin/bbsctl
	chown root /home/bbs/bin/bbsctl
	chmod 4755 /home/bbs/bin/bbsctl

