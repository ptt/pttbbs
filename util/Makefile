# $Id$

.include "../pttbbs.mk"

CFLAGS+= -DPTTBBS_UTIL

BBSBASE=	../include/var.h

UTIL_OBJS=	\
	util_cache.o	util_record.o	util_passwd.o	util_var.o	\
	util_stuff.o	util_osdep.o	util_args.o	util_file.o	\
	util_crypt.o

MBBSD_OBJS= \
	cache		record		passwd		var		\
	stuff		osdep		args		file		\
	crypt

# 下面這些程式, 會被 compile 並且和 $(UTIL_OBJS) 聯結
CPROG_WITH_UTIL= \
	boardlist	BM_money	post		poststat	\
	jungo		account		birth		deluserfile	\
	expire		mandex		rmuid		horoscope	\
	openvice	parse_news	openticket	topusr		\
	yearsold	toplazyBM	toplazyBBM	\
	reaper		buildAnnounce	inndBM		shmctl		\
	outmail		chkhbf		checkmoney      merge_dir       \
	transman	angel		gamblegive	checkdir	\
	chesscountry	tunepasswd	buildir		xchatd

# 下面這些程式, 會直接被 compile
CPROG_WITHOUT_UTIL= \
	uhash_loader	showboard	\
	countalldice	webgrep		bbsrf		initbbs		\
	userlist	merge_passwd	\
	merge_board	bbsmail         

# 下面這些程式會被 install
PROGS=	${CPROG_WITH_UTIL}		${CPROG_WITHOUT_UTIL}		\
	BM_money.sh	backpasswd.sh	mailog.sh	opendice.sh	\
	openticket.sh	stock.sh	topsong.sh	weather.sh	\
	stock.perl	weather.perl	toplazyBM.sh	toplazyBBM.sh	\
	dailybackup.pl	tarqueue.pl	waterball.pl	filtermail.pl	\
	getbackup.pl	udnnews.pl	rebuildaloha.pl	railway_wrapper.pl

all: ${CPROG_WITH_UTIL} ${CPROG_WITHOUT_UTIL} ${PROGS}

../include/var.h: ../mbbsd/var.c
	cd ../mbbsd; $(MAKE) ../include/var.h

.for fn in ${CPROG_WITH_UTIL}
${fn}: ${BBSBASE} ${fn}.c ${UTIL_OBJS}
	$(CCACHE) ${CC} ${CFLAGS} ${LDFLAGS} -o ${fn} ${UTIL_OBJS} ${fn}.c
.endfor

.for fn in ${MBBSD_OBJS}
util_${fn}.o: ${BBSBASE} ../mbbsd/${fn}.c
	$(CCACHE) ${CC} ${CFLAGS} -D_BBS_UTIL_C_ -c -o $@ ../mbbsd/${fn}.c
.endfor

bbsmail: ${BBSBASE} bbsmail.c ../innbbsd/str_decode.c $(UTIL_OBJS)
	$(CCACHE) $(CC) $(CFLAGS) $(LDFLAGS) -o bbsmail -DUSE_ICONV \
	bbsmail.c ../innbbsd/str_decode.c $(UTIL_OBJS)

install: $(PROGS)
	install -d $(BBSHOME)/bin/
	install -c -m 755 $(PROGS) $(BBSHOME)/bin/
	chmod 4755 $(BBSHOME)/bin/post
.if defined(WITHFILTERMAIL)
	$(MAKE) installfiltermail
.endif

clean:
	rm -f *.o $(CPROGS) $(CPROG_WITH_UTIL) $(CPROG_WITHOUT_UTIL)


installfiltermail:
	mv $(BBSHOME)/bin/bbsmail $(BBSHOME)/bin/realbbsmail
	ln -s $(BBSHOME)/bin/filtermail.pl $(BBSHOME)/bin/bbsmail

# for diskstat(FreeBSD 4.x only) .
# diskstat should be compiled with bbs and installed with root
diskstat: diskstat.c
	$(CCACHE) $(CC) $(CFLAGS) -o diskstat diskstat.c -ldevstat -lkvm

installdiskstat: diskstat
	cp -f diskstat /usr/local/bin/
	chgrp kmem /usr/local/bin/diskstat
	chmod 2755 /usr/local/bin/diskstat

# for bbsctl. bbsctl should be compiled with bbs and installed with root
bbsctl:	bbsctl.c
	$(CCACHE) $(CC) $(CFLAGS) -o $@ $@.c

installbbsctl: bbsctl
	rm -f /home/bbs/bin/bbsctl
	cp /home/bbs/pttbbs/util/bbsctl /home/bbs/bin/bbsctl
	chown root /home/bbs/bin/bbsctl
	chmod 4755 /home/bbs/bin/bbsctl

cleanpasswd: cleanpasswd.c ${UTIL_OBJS}
	$(CCACHE) ${CC} ${CFLAGS} ${LDFLAGS} -o cleanpasswd ${UTIL_OBJS} cleanpasswd.c

r2014transfer: r2014convert
	$(CCACHE) ${CC} ${CFLAGS} ${LDFLAGS} -o r2014convert r2014convert.c
	./r2014convert
	rm r2014convert

passwdconverter: passwdconverter.c
	$(CCACHE) $(CC) $(CFLAGS) $(LDFLAGS) $(UTIL_OBJS) -o passwdconverter passwdconverter.c
