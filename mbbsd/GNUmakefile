SRCROOT=	..
include $(SRCROOT)/pttbbs.mk

#######################################################################
# common modules
#######################################################################

PROG=	mbbsd
COREOBJS = bbs.o announce.o read.o board.o brc.o mail.o record.o fav.o
ABUSEOBJS = captcha.o
ACCOBJS  = user.o acl.o register.o passwd.o emaildb.o
NETOBJS  = mbbsd.o io.o term.o telnet.o nios.o
TALKOBJS = friend.o talk.o ccw.o
UTILOBJS = stuff.o kaede.o convert.o name.o syspost.o cache.o cal.o tag.o
UIOBJS   = menu.o vtuikit.o psb.o
PAGEROBJS= more.o pmore.o pager.o
PLUGOBJS = ordersong.o angel.o timecap.o aids.o
CHESSOBJS= chess.o chc.o chc_tab.o ch_go.o ch_gomo.o ch_dark.o ch_reversi.o ch_conn6.o
GAMEOBJS = chicken.o gamble.o
OBJS:=	admin.o assess.o edit.o xyz.o var.o vote.o voteboard.o comments.o \
	$(COREOBJS) $(ABUSEOBJS) $(ACCOBJS) $(NETOBJS) $(TALKOBJS) $(UTILOBJS) \
	$(UIOBJS) $(PAGEROBJS) $(PLUGOBJS) \
	$(CHESSOBJS) $(GAMEOBJS)

ifneq ($(wildcard local.c),)
OBJS+=	local.o
endif

# reduce .bss align overhead
ifndef DEBUG
ifneq ($(OSTYPE),Darwin)
LDFLAGS+=-Wl,--sort-common
endif
endif

#######################################################################
# common libraries
#######################################################################

LDFLAGS+= -L$(SRCROOT)/common/bbs -L$(SRCROOT)/common/sys \
	  -L$(SRCROOT)/common/osdep
LDLIBS:= -lcmbbs -lcmsys -losdep $(LDLIBS)

ifdef WITH_OUTRPT
CFLAGS+= -DDBG_OUTRPT
endif

#######################################################################
# conditional configurations and optional modules
#######################################################################

USE_BBSLUA!=  	sh -c '${DEF_CMD}"USE_BBSLUA" ${BBSCONF} ${DEF_YES}'
USE_PFTERM:=	$(call DEF_CHECK,USE_PFTERM)
USE_NIOS:=	$(call DEF_CHECK,USE_NIOS)

ifneq ($(strip $(USE_BBSLUA)),)
ifeq ($(OSTYPE),FreeBSD)
LUA_PKG_NAME?=	lua-5.1
LDLIBS+=	-Wl,--no-as-needed
else
LUA_PKG_NAME?=	lua5.1
endif
LUA_CFLAGS:=	$(shell pkg-config --cflags ${LUA_PKG_NAME})
LUA_LIBS:=	$(shell pkg-config --libs ${LUA_PKG_NAME})
CFLAGS+=	${LUA_CFLAGS}
LDLIBS+=	${LUA_LIBS}
OBJS+=		bbslua.o bbsluaext.o
endif

ifneq ($(strip $(USE_PFTERM)),)
OBJS+=		pfterm.o
else
OBJS+=		screen.o
endif

ifneq ($(strip $(USE_MBBSD_CXX)),)
CXXFLAGS+=	-std=c++17 -I$(SRCROOT)/common/fbs
LDLIBS+=	-lstdc++
OBJS+=		recover.o register_sms.o \
		verifydb.o verifydb_info.o verifydb_search.o
endif

#######################################################################
# variables
#######################################################################

DATETIME:=	$(shell date '+%Y%m%d%H%M')

#######################################################################
# Make Rules
#######################################################################

.SUFFIXES: .c .o
.PHONY: all ctags test install clean

%.o: %.c $(SRCROOT)/include/var.h
	$(CC) $(CFLAGS) -c $*.c

all: $(PROG)

$(PROG): testsz $(OBJS)
	sh $(SRCROOT)/util/newvers.sh
	@printf "\033[0;1;32m"
	@[ -z "$(DEBUG)" ] || printf "\033[31m+DEBUG \033[32m"
	@[ -z "$(USE_BBSLUA)" ] || echo -n "+BBSLUA "
	@[ -z "$(USE_PFTERM)" ] || echo -n "+PFTERM "
	@[ -z "$(USE_NIOS)" ] || echo -n "+NIOS "
	@[ -z "$(USE_MBBSD_CXX)" ] || echo -n "+CXX "
	@printf "\033[m"
	$(CC) vers.c -o $(PROG) $(OBJS) $(LDFLAGS) $(LDLIBS)

testsz:	$(SRCROOT)/pttbbs.conf $(SRCROOT)/include/*.h testsz.c
	$(CC) $(CFLAGS) testsz.c -o testsz $(LDFLAGS) $(LDLIBS)
	@printf "\033[0;1;33mChecking configuration data sizes...\033[m\n"
	@./testsz
	@printf "\033[0;1;32mData size configuration OK\033[m\n"

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
	install -s $(PROG) $(BBSHOME)/bin/mbbsd.$(DATETIME)
	objcopy --only-keep-debug --compress-debug-sections $(PROG) $(BBSHOME)/bin/mbbsd.$(DATETIME).debug
	@(cd $(BBSHOME)/bin && \
	  objcopy --add-gnu-debuglink=mbbsd.$(DATETIME).debug mbbsd.$(DATETIME) && \
	  ln -sfv mbbsd.$(DATETIME) $(BBSHOME)/bin/mbbsd)

clean:
	rm -f $(OBJS) $(PROG)
