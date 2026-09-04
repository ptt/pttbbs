SRCROOT ?= ..
include $(SRCROOT)/pttbbs.mk

CFLAGS   += -DPTTBBS_UTIL
CXXFLAGS += -std=c++11

BBSBASE := $(SRCROOT)/include/var.h

UTIL_OBJS := util_var.o

END :=

# 下面這些程式, 會被 compile 並且和 $(UTIL_OBJS) 聯結
CPROG_WITH_UTIL := \
	boardlist	post		poststat	\
	account		deluserfile	\
	expire		mandex		broadcast	\
	openticket	topusr		\
	toplazyBM	writemoney	\
	reaper		buildAnnounce	mailangel	\
	outmail		chkhbf		userlist	\
	angel		gamblegive	\
	tunepasswd	buildir		xchatd		\
	uhash_loader	timecap_buildref showuser	removebm \
	redir		permreport	setrole 	update_online \
	munin		useractive_munin	\
	$(END)

# 下面是 C++ 的程式
CPP_WITH_UTIL := \
	mergedir2	buildir2    \
	$(END)

# 下面這些程式, 會直接被 compile
CPROG_WITHOUT_UTIL := \
	showboard	bbsrf		initbbs		merge_board	\
	bbsmail		\
	$(END)

SCRIPT_PROGS := \
	backpasswd.sh	mailog.sh	openticket.sh	topsong.sh	\
	toplazyBM.sh	dailybackup.pl	tarqueue.pl	waterball.pl	\
	filtermail.pl	getbackup.pl	rebuildaloha.pl	timecap_expire.sh	\
	$(END)

# 下面這些程式會被 install
CPROG_WITH_UTIL_BUILD    := $(foreach p,$(CPROG_WITH_UTIL),$(if $(wildcard $(p).c),$(p)))
CPP_WITH_UTIL_BUILD       := $(foreach p,$(CPP_WITH_UTIL),$(if $(wildcard $(p).cc),$(p)))
CPROG_WITHOUT_UTIL_BUILD := $(foreach p,$(CPROG_WITHOUT_UTIL),$(if $(wildcard $(p).c),$(p)))
SCRIPT_PROGS_BUILD       := $(foreach s,$(SCRIPT_PROGS),$(if $(wildcard $(s)),$(s)))

COMPILED_PROGS := $(CPROG_WITH_UTIL_BUILD) $(CPROG_WITHOUT_UTIL_BUILD) $(CPP_WITH_UTIL_BUILD) shmctl

PROGS := $(COMPILED_PROGS) $(SCRIPT_PROGS_BUILD)

LDLIBS := $(SRCROOT)/common/bbs/libcmbbs.a \
	  $(SRCROOT)/common/sys/libcmsys.a \
	  $(SRCROOT)/common/osdep/libosdep.a \
	  $(LDLIBS)

.PHONY: all clean install installfiltermail bbsctl installbbsctl

all: $(BBSBASE) $(PROGS)

$(CPROG_WITH_UTIL_BUILD): %: %.c $(BBSBASE) $(UTIL_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(UTIL_OBJS) $< $(LDLIBS)

$(CPP_WITH_UTIL_BUILD): %: %.cc $(BBSBASE) $(UTIL_OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(UTIL_OBJS) $< $(LDLIBS)

util_var.o: $(BBSBASE) $(SRCROOT)/mbbsd/var.c
	$(CC) $(CFLAGS) -D_BBS_UTIL_C_ -c -o $@ $(SRCROOT)/mbbsd/var.c

shmctl: shmctl.c $(BBSBASE) $(UTIL_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(UTIL_OBJS) $(LDLIBS)

bbsmail: bbsmail.c $(BBSBASE) $(UTIL_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(UTIL_OBJS) $(LDLIBS)

install: $(PROGS)
	install -d $(BBSHOME)/bin/
	install -c -m 755 $(PROGS) $(BBSHOME)/bin/
	chmod 4755 $(BBSHOME)/bin/post
ifdef WITHFILTERMAIL
	$(MAKE) installfiltermail
endif

clean:
	rm -f *.o $(COMPILED_PROGS)

installfiltermail:
	mv $(BBSHOME)/bin/bbsmail $(BBSHOME)/bin/realbbsmail
	ln -sf $(BBSHOME)/bin/filtermail.pl $(BBSHOME)/bin/bbsmail

# for bbsctl. bbsctl should be compiled with bbs and installed with root
bbsctl: bbsctl.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

installbbsctl: bbsctl
	rm -f $(BBSHOME)/bin/bbsctl
	cp $(SRCROOT)/util/bbsctl $(BBSHOME)/bin/bbsctl
	chown root $(BBSHOME)/bin/bbsctl
	chmod 4755 $(BBSHOME)/bin/bbsctl

cleanpasswd: cleanpasswd.c $(BBSBASE) $(UTIL_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(UTIL_OBJS) $(LDLIBS)
