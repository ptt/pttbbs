# $Id$

.include "../pttbbs.mk"

CFLAGS+= -DPTTBBS_UTIL

BBSBASE=	../include/var.h

UTIL_OBJS=	\
	util_cache.o	util_record.o	util_passwd.o	util_var.o	\
	util_stuff.o	util_osdep.o	util_args.o

MBBSD_OBJS= \
	cache		record		passwd		var		\
	stuff		osdep		args

# 下面這些程式, 會被 compile 並且和 $(UTIL_OBJS) 聯結
CPROG_WITH_UTIL= \
	boardlist	BM_money	post		poststat	\
	jungo		account		birth		deluserfile	\
	expire		mandex		rmuid		horoscope	\
	openvice	parse_news	openticket	topusr		\
	indexuser	yearsold	toplazyBM	toplazyBBM	\
	reaper		buildAnnounce	inndBM		shmctl		\
	outmail

# 下面這些程式, 會直接被 compile
CPROG_WITHOUT_UTIL= \
	shmsweep	uhash_loader	showboard	antispam	\
	countalldice	webgrep		bbsrf		initbbs		\
	userlist	tunepasswd	buildir		merge_passwd	\
	merge_board	xchatd

# 下面這些程式會被 install
PROGS=	${UTIL_OBJS}	${CPROG_WITH_UTIL}	${CPROG_WITHOUT_UTIL}	\
	BM_money.sh	backpasswd.sh	mailog.sh	opendice.sh	\
	openticket.sh	stock.sh	topsong.sh	weather.sh	\
	stock.perl	weather.perl	toplazyBM.sh	toplazyBBM.sh	\
	dailybackup.pl	tarqueue.pl	waterball.pl	filtermail.pl	\
	getbackup.pl	udnnews.pl	rebuildaloha.pl	railway_wrapper.pl\
	bbsmail

all: ${CPROG_WITH_UTIL} ${CPROG_WITHOUT_UTIL} ${PROGS}

../include/var.h: ../mbbsd/var.c
	cd ../mbbsd; make ../include/var.h

.for fn in ${CPROG_WITH_UTIL}
${fn}: ${BBSBASE} ${fn}.c ${UTIL_OBJS}
	${CC} ${CFLAGS} ${LDFLAGS} -o ${fn} ${UTIL_OBJS} ${fn}.c
.endfor

.for fn in ${MBBSD_OBJS}
util_${fn}.o: ${BBSBASE} ../mbbsd/${fn}.c
	${CC} ${CFLAGS} -D_BBS_UTIL_C_ -c -o $@ ../mbbsd/${fn}.c
.endfor

xchatd: ${BBSBASE} xchatd.c  $(UTIL_OBJS) descrypt.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $@.c  $(UTIL_OBJS) descrypt.c

bbsmail: ${BBSBASE} bbsmail.c ../innbbsd/str_decode.c $(UTIL_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o bbsmail -DUSE_ICONV \
	bbsmail.c ../innbbsd/str_decode.c $(UTIL_OBJS)

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

cleanpasswd: cleanpasswd.c ${UTIL_OBJS}
	${CC} ${CFLAGS} ${LDFLAGS} -o cleanpasswd ${UTIL_OBJS} cleanpasswd.c
