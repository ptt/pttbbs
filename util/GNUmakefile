SRCROOT=	..
include $(SRCROOT)/pttbbs.mk

CFLAGS+= -DPTTBBS_UTIL
CXXFLAGS+= -std=c++11

BBSBASE=	$(SRCROOT)/include/var.h

UTIL_OBJS= 	util_var.o

MBBSD_OBJS= 	var

# 下面這些程式, 會被 compile 並且和 $(UTIL_OBJS) 聯結
CPROG_WITH_UTIL= \
	boardlist	post		poststat	\
	account		deluserfile	\
	expire		mandex		broadcast	\
	openticket	topusr		\
	toplazyBM	writemoney	\
	reaper		buildAnnounce	mailangel	\
	outmail		chkhbf		userlist	\
	angel		gamblegive	\
	chesscountry	tunepasswd	buildir		xchatd		\
	uhash_loader	timecap_buildref showuser	removebm \
	redir		permreport	setrole 	update_online \
	munin		useractive_munin	\

# 下面是 C++ 的程式
CPP_WITH_UTIL= \
	mergedir2	buildir2


# 下面這些程式, 會直接被 compile
CPROG_WITHOUT_UTIL= \
	showboard	bbsrf		\
	initbbs		merge_board	bbsmail

# 下面這些程式會被 install
PROGS=	${CPROG_WITH_UTIL}	${CPROG_WITHOUT_UTIL}	${CPP_WITH_UTIL}\
	shmctl	\
	backpasswd.sh	mailog.sh	\
	openticket.sh	topsong.sh	toplazyBM.sh	\
	dailybackup.pl	tarqueue.pl	waterball.pl	filtermail.pl	\
	getbackup.pl	rebuildaloha.pl timecap_expire.sh

LDLIBS:=$(SRCROOT)/common/bbs/libcmbbs.a \
	$(SRCROOT)/common/sys/libcmsys.a \
	$(SRCROOT)/common/osdep/libosdep.a \
	$(LDLIBS)

.PHONY: all clean install installfiltermail bbsctl installbbsctl

all: ${SRCROOT}/include/var.h \
     ${CPROG_WITH_UTIL} ${CPROG_WITHOUT_UTIL} ${CPP_WITH_UTIL} ${PROGS}

$(CPROG_WITH_UTIL): %: %.c ${BBSBASE} ${UTIL_OBJS}
	${CC} ${CFLAGS} ${LDFLAGS} -o $@ ${UTIL_OBJS} $< $(LDLIBS)

$(CPP_WITH_UTIL): %: %.cc ${BBSBASE} ${UTIL_OBJS}
	$(CXX) ${CXXFLAGS} ${LDFLAGS} -o $@ ${UTIL_OBJS} $< $(LDLIBS)

util_var.o: ${BBSBASE} $(SRCROOT)/mbbsd/var.c
	${CC} ${CFLAGS} -D_BBS_UTIL_C_ -c -o $@ $(SRCROOT)/mbbsd/var.c

shmctl: ${BBSBASE} shmctl.c ${UTIL_OBJS}
	${CC} ${CFLAGS} ${LDFLAGS} -o shmctl ${UTIL_OBJS} shmctl.c $(LDLIBS)

bbsmail: ${BBSBASE} bbsmail.c $(UTIL_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o bbsmail bbsmail.c $(UTIL_OBJS) $(LDLIBS)

install: $(PROGS)
	install -d $(BBSHOME)/bin/
	install -c -m 755 $(PROGS) $(BBSHOME)/bin/
	chmod 4755 $(BBSHOME)/bin/post
ifdef WITHFILTERMAIL
	$(MAKE) installfiltermail
endif

clean:
	rm -f *.o shmctl $(CPROGS) $(CPROG_WITH_UTIL) $(CPROG_WITHOUT_UTIL) $(CPP_WITH_UTIL)


installfiltermail:
	mv $(BBSHOME)/bin/bbsmail $(BBSHOME)/bin/realbbsmail
	ln -s $(BBSHOME)/bin/filtermail.pl $(BBSHOME)/bin/bbsmail

# for bbsctl. bbsctl should be compiled with bbs and installed with root
bbsctl:	bbsctl.c
	$(CC) $(CFLAGS) ${LDFLAGS} -o $@ $@.c $(LDLIBS)

installbbsctl: bbsctl
	rm -f /home/bbs/bin/bbsctl
	cp /home/bbs/pttbbs/util/bbsctl /home/bbs/bin/bbsctl
	chown root /home/bbs/bin/bbsctl
	chmod 4755 /home/bbs/bin/bbsctl

cleanpasswd: cleanpasswd.c ${UTIL_OBJS}
	${CC} ${CFLAGS} ${LDFLAGS} -o cleanpasswd ${UTIL_OBJS} cleanpasswd.c
