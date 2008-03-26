# $Id$

SRCROOT=	..
.include "$(SRCROOT)/pttbbs.mk"

CFLAGS+= -DPTTBBS_UTIL

BBSBASE=	$(SRCROOT)/include/var.h

UTIL_OBJS=	\
	util_cache.o	util_record.o	util_passwd.o	util_var.o	\
	util_stuff.o

MBBSD_OBJS= \
	cache		record		passwd		var		\
	stuff

# 下面這些程式, 會被 compile 並且和 $(UTIL_OBJS) 聯結
CPROG_WITH_UTIL= \
	boardlist	BM_money	post		poststat	\
	jungo		account		birth		deluserfile	\
	expire		mandex		horoscope	broadcast	\
	openvice	openticket	topusr		\
	yearsold	toplazyBM	toplazyBBM	writemoney	\
	reaper		buildAnnounce	inndBM		mailangel	\
	outmail		chkhbf		merge_dir	\
	transman	angel		gamblegive	wretch_man	\
	chesscountry	tunepasswd	buildir		xchatd		\
	uhash_loader

# 下面是 C++ 的程式
CPP_WITH_UTIL= \


# 下面這些程式, 會直接被 compile
CPROG_WITHOUT_UTIL= \
	showboard	countalldice	bbsrf		\
	initbbs		userlist	merge_board	bbsmail

# 下面這些程式會被 install
PROGS=	${CPROG_WITH_UTIL}	${CPROG_WITHOUT_UTIL}	${CPP_WITH_UTIL}\
	shmctl	\
	BM_money.sh	backpasswd.sh	mailog.sh	opendice.sh	\
	openticket.sh	stock.sh	topsong.sh	weather.sh	\
	stock.perl	weather.perl	toplazyBM.sh	toplazyBBM.sh	\
	dailybackup.pl	tarqueue.pl	waterball.pl	filtermail.pl	\
	getbackup.pl	udnnews.pl	rebuildaloha.pl

LIBS+=  $(SRCROOT)/src/libbbsutil/libbbsutil.a \
	$(SRCROOT)/src/libbbs/libbbs.a


all: ${CPROG_WITH_UTIL} ${CPROG_WITHOUT_UTIL} ${CPP_WITH_UTIL} ${PROGS}

$(SRCROOT)/include/var.h: $(SRCROOT)/mbbsd/var.c
	cd $(SRCROOT)/mbbsd; $(MAKE) $(SRCROOT)/include/var.h

.for fn in ${CPROG_WITH_UTIL}
${fn}: ${BBSBASE} ${fn}.c ${UTIL_OBJS}
	$(CCACHE) ${CC} ${CFLAGS} ${LDFLAGS} -o ${fn} ${UTIL_OBJS} ${fn}.c $(LIBS)
.endfor

.for fn in ${CPP_WITH_UTIL}
${fn}: ${BBSBASE} ${fn}.cc ${UTIL_OBJS}
	$(CCACHE) g++ ${CFLAGS} ${LDFLAGS} -o ${fn} ${UTIL_OBJS} ${fn}.cc $(LIBS)
.endfor

.for fn in ${MBBSD_OBJS}
util_${fn}.o: ${BBSBASE} $(SRCROOT)/mbbsd/${fn}.c
	$(CCACHE) ${CC} ${CFLAGS} -D_BBS_UTIL_C_ -c -o $@ $(SRCROOT)/mbbsd/${fn}.c
.endfor

shmctl: ${BBSBASE} shmctl.c ${UTIL_OBJS}
	$(CCACHE) ${CXX} ${CFLAGS} ${LDFLAGS} -o shmctl ${UTIL_OBJS} shmctl.c $(LIBS)
#shmctl: ${BBSBASE} shmctl.c ${UTIL_OBJS}
#	$(CCACHE) gcc -g -DBBSHOME='"/home/bbs"' -I$(SRCROOT)/include  -D__OS_MAJOR_VERSION__="2"  -D__OS_MINOR_VERSION__="6" -DPTTBBS_UTIL  -O1  -o shmctl ${UTIL_OBJS} shmctl.c
#shmctl: ${BBSBASE} shmctl.cc ${UTIL_OBJS}
#	$(CCACHE) g++ -g -DBBSHOME='"/home/bbs"' -I$(SRCROOT)/include  -D__OS_MAJOR_VERSION__="2"  -D__OS_MINOR_VERSION__="6" -DPTTBBS_UTIL  -O1 -o shmctl ${UTIL_OBJS} shmctl.cc

bbsmail: ${BBSBASE} bbsmail.c $(SRCROOT)/innbbsd/str_decode.c $(UTIL_OBJS)
	$(CCACHE) $(CC) $(CFLAGS) $(LDFLAGS) -o bbsmail -DUSE_ICONV \
	bbsmail.c $(SRCROOT)/innbbsd/str_decode.c $(UTIL_OBJS) $(LIBS)

install: $(PROGS)
	install -d $(BBSHOME)/bin/
	install -c -m 755 $(PROGS) $(BBSHOME)/bin/
	chmod 4755 $(BBSHOME)/bin/post
.if defined(WITHFILTERMAIL)
	$(MAKE) installfiltermail
.endif

clean:
	rm -f *.o $(CPROGS) $(CPROG_WITH_UTIL) $(CPROG_WITHOUT_UTIL) $(CPP_WITH_UTIL)


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
bbsctl:	bbsctl.c util_osdep.o
	$(CCACHE) $(CC) $(CFLAGS) ${LDFLAGS} -o $@ $@.c util_osdep.o

installbbsctl: bbsctl
	rm -f /home/bbs/bin/bbsctl
	cp /home/bbs/pttbbs/util/bbsctl /home/bbs/bin/bbsctl
	chown root /home/bbs/bin/bbsctl
	chmod 4755 /home/bbs/bin/bbsctl

cleanpasswd: cleanpasswd.c ${UTIL_OBJS}
	$(CCACHE) ${CC} ${CFLAGS} ${LDFLAGS} -o cleanpasswd ${UTIL_OBJS} cleanpasswd.c
