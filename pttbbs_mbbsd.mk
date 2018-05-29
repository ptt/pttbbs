#######################################################################
#
# FROM mbbsd/Makefile
#
#######################################################################

COREOBJS = bbs.o announce.o read.o board.o brc.o mail.o record.o fav.o
ABUSEOBJS = captcha.o
ACCOBJS  = user.o acl.o register.o passwd.o emaildb.o
#NETOBJS  = mbbsd.o io.o term.o telnet.o nios.o
NETOBJS  = io.o term.o telnet.o nios.o
TALKOBJS = friend.o talk.o ccw.o
UTILOBJS = stuff.o kaede.o convert.o name.o syspost.o cache.o cal.o
UIOBJS   = menu.o vtuikit.o psb.o
PAGEROBJS= more.o pmore.o
PLUGOBJS = ordersong.o angel.o timecap.o
CHESSOBJS= chess.o chc.o chc_tab.o ch_go.o ch_gomo.o ch_dark.o ch_reversi.o ch_conn6.o
GAMEOBJS = chicken.o gamble.o
OBJS:=	admin.o assess.o edit.o xyz.o var.o vote.o voteboard.o comments.o \
	$(COREOBJS) $(ABUSEOBJS) $(ACCOBJS) $(NETOBJS) $(TALKOBJS) $(UTILOBJS) \
	$(UIOBJS) $(PAGEROBJS) $(PLUGOBJS) \
	$(CHESSOBJS) $(GAMEOBJS)

#######################################################################
# special library (DIET) configuration
#######################################################################

.if defined(DIET)
LDFLAGS+=	-L$(SRCROOT)/common/diet
LDLIBS+=	-ldiet
DIETCC:=	diet -Os
.endif

# reduce .bss align overhead
.if !defined(DEBUG) && $(OSTYPE)!="Darwin"
LDFLAGS+=-Wl,--sort-common
.endif

#######################################################################
# common libraries
#######################################################################

LDFLAGS+= -L$(SRCROOT)/common/bbs -L$(SRCROOT)/common/sys \
	  -L$(SRCROOT)/common/osdep -L$(SRCROOT)/common/pttlib -L$(SRCROOT)/common/pttui
LDLIBS:= -lcmbbs -lcmsys -losdep -lcmpttlib -lcmpttui $(LDLIBS)

#######################################################################
# conditional configurations and optional modules
#######################################################################

.if $(USE_BBSLUA)
.if $(OSTYPE)=="FreeBSD"
LUA_PKG_NAME?=	lua-5.1
LDLIBS+=	-Wl,--no-as-needed
.else
LUA_PKG_NAME?=	lua5.1
.endif
LUA_CFLAGS!=	pkg-config --cflags ${LUA_PKG_NAME}
LUA_LIBS!=	pkg-config --libs ${LUA_PKG_NAME}
CFLAGS+=	${LUA_CFLAGS}
LDLIBS+=	${LUA_LIBS}
OBJS+=		bbslua.o bbsluaext.o
.endif

.if $(USE_PFTERM)
OBJS+=		pfterm.o
.else
OBJS+=		screen.o
.endif

.if $(USE_PTTUI_THREAD)
LDLIBS+=	-pthread
.endif
