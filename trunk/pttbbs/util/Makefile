# $Id: Makefile,v 1.37 2003/07/21 09:27:00 in2 Exp $

.include "../pttbbs.mk"

UTIL_OBJS=	\
	util_cache.o util_record.o util_passwd.o util_var.o util_stuff.o

# 下面這些程式, 會被 compile 並且和 $(UTIL_OBJS) 聯結
CPROG_WITH_UTIL= \
	boardlist	BM_money	post		poststat	\
	jungo		account		birth		deluserfile	\
	expire		mandex		rmuid		horoscope	\
	openvice	parse_news	openticket	topusr		\
	indexuser	yearsold	toplazyBM	toplazyBBM	\
	reaper		buildAnnounce	inndBM		shmctl

# 下面這些程式, 會直接被 compile
CPROG_WITHOUT_UTIL= \
	shmsweep	uhash_loader	showboard	antispam	\
	countalldice	webgrep		bbsrf		initbbs		\
	userlist	tunepasswd	buildir		merge_passwd	\
	merge_board	xchatd		outmail		bbsmail

# 下面這些程式會被 install
PROGS=	${UTIL_OBJS}	${CPROG_WITH_UTIL}	${CPROG_WITHOUT_UTIL}	\
	BM_money.sh	backpasswd.sh	mailog.sh	opendice.sh	\
	openticket.sh	stock.sh	topsong.sh	weather.sh	\
	stock.perl	weather.perl	toplazyBM.sh	toplazyBBM.sh	\
	dailybackup.pl	tarqueue.pl	waterball.pl	filtermail.pl	\
	getbackup.pl	udnnews.pl	rebuildaloha.pl	railway_wrapper.pl

all: ${CPROG_WITH_UTIL} ${CPROG_WITHOUT_UTIL} ${PROGS}

.for fn in ${CPROG_WITH_UTIL}
${fn}:	${fn}.c ${UTIL_OBJS}
	${CC} ${CFLAGS} -o ${fn} ${UTIL_OBJS} ${fn}.c
.endfor

util_var.o: ../mbbsd/var.c
	${CC} ${CFLAGS} -D_BBS_UTIL_C_ -c -o util_var.o ../mbbsd/var.c

util_stuff.o: ../mbbsd/stuff.c
	${CC} ${CFLAGS} -D_BBS_UTIL_C_ -c -o util_stuff.o ../mbbsd/stuff.c

util_cache.o: ../mbbsd/cache.c
	${CC} ${CFLAGS} -D_BBS_UTIL_C_ -c -o util_cache.o ../mbbsd/cache.c

bbsmail: bbsmail.c ${UTIL_OBJS} ../mbbsd/osdep.o
	${CC} ${CFLAGS} -o bbsmail bbsmail.c ${UTIL_OBJS} ../mbbsd/osdep.o ${LDFLAGS}

xchatd: xchatd.c  $(UTIL_OBJS) descrypt.c
	$(CC) $(CFLAGS) -o $@ $@.c  $(UTIL_OBJS) descrypt.c $(LIBCHAT)

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

