BBSHOME?=	$(HOME)
BBSHOME?=	/home/bbs

SRCROOT?=	.
OSTYPE:=	$(shell uname)

# Detect best compiler
#
CC:=		gcc
CXX:=		g++

CLANG:=		$(shell type clang >/dev/null 2>&1 && echo 1 || echo 0)
CCACHE:=	$(shell type ccache >/dev/null 2>&1 && echo 1 || echo 0)

ifdef WITHOUT_CLANG
CLANG:=
else ifneq ($(strip $(CLANG)),0)
CC:=		clang
CXX:=		clang++
endif

ifneq ($(strip $(CCACHE)),0)
CC:=		ccache $(CC)
CXX:=		ccache $(CXX)
endif

# Common build flags

PTT_WARN:=	-W -Wall -Wunused \
    		-Wno-missing-field-initializers -Wno-address-of-packed-member \
    		-Werror=format
PTT_CFLAGS:=	$(PTT_WARN) -pipe -DBBSHOME='"$(BBSHOME)"' -I$(SRCROOT)/include
PTT_CXXFLAGS:=	$(PTT_WARN) -pipe -DBBSHOME='"$(BBSHOME)"' -I$(SRCROOT)/include
PTT_LDFLAGS:=	-Wl,--as-needed
ifneq ($(strip $(CLANG)),0)
PTT_CFLAGS+=	-Qunused-arguments -Wno-parentheses-equality \
		-fcolor-diagnostics -Wno-invalid-source-encoding
PTT_CXXFLAGS+=	-Wno-invalid-source-encoding
endif

# enable assert()
#PTT_CFLAGS+=	-DNDEBUG 

# Platform specific build flags

ifeq (${OSTYPE},Darwin)
PTT_CFLAGS+=	-I/opt/local/include -DNEED_SETPROCTITLE
PTT_CXXFLAGS+=	-I/opt/local/include
PTT_LDFLAGS+=	-L/opt/local/lib
PTT_LDLIBS+=	-liconv
endif

ifeq (${OSTYPE},Linux)
PTT_LDLIBS+=    -lrt -pthread
endif

ifeq (${OSTYPE},FreeBSD)
# FreeBSD特有的環境
PTT_CFLAGS+=	-I/usr/local/include
PTT_CXXFLAGS+=	-I/usr/local/include
PTT_LDFLAGS+=	-L/usr/local/lib
PTT_LDLIBS+=	-lkvm -liconv
endif

# 若有定義 PROFILING
ifdef PROFILING
PTT_CFLAGS+=	-pg
PTT_CXXFLAGS+=	-pg
PTT_LDFLAGS+=	-pg
NO_OMITFP=	yes
NO_FORK=	yes
endif

# 若有定義 DEBUG, 則在 CFLAGS內定義 DEBUG
ifdef DEBUG
GDB=		1
PTT_CFLAGS+=	-DDEBUG 
PTT_CXXFLAGS+=	-DDEBUG 
endif

ifdef GDB
CFLAGS:=	-g -O0 $(PTT_CFLAGS)
CXXFLAGS:=	-g -O0 $(PTT_CXXFLAGS)
LDFLAGS:=	-O0 $(PTT_LDFLAGS)
LDLIBS:=	$(PTT_LDLIBS)
else
CFLAGS:=	-g -Os $(PTT_CFLAGS) $(EXT_CFLAGS)
CXXFLAGS:=	-g -Os $(PTT_CXXFLAGS) $(EXT_CXXFLAGS)
LDFLAGS:=	-Os $(PTT_LDFLAGS)
LDLIBS:=	$(PTT_LDLIBS)

ifdef OMITFP
CFLAGS+=	-fomit-frame-pointer
CXXFLAGS+=	-fomit-frame-pointer
endif
endif

LDADD=		$(LDLIBS)

# 若有定義 NO_FORK, 則在 CFLAGS內定義 NO_FORK
ifdef NO_FORK
CFLAGS+=	-DNO_FORK
CXXFLAGS+=	-DNO_FORK
endif

######################################
# Settings for common libraries

#######################################################################
# conditional configurations and optional modules
#######################################################################

BBSCONF:=       $(SRCROOT)/pttbbs.conf
DEF_PATTERN:=   ^[ \t]*\#[ \t]*define[ \t]*
DEF_CMD:=       grep -Ewq "${DEF_PATTERN}"
DEF_YES:=       && echo "YES" || echo ""

#libevent
LIBEVENT_CFLAGS:=	$(shell (pkg-config --cflags libevent_pthreads || true) 2>/dev/null)
LIBEVENT_LIBS_L:=	$(shell (pkg-config --libs-only-L libevent_pthreads || true) 2>/dev/null)
LIBEVENT_LIBS_l:=	$(shell (pkg-config --libs-only-l libevent_pthreads || true) 2>/dev/null)

# grpc++
GRPCPP_CFLAGS:=		$(shell (pkg-config --cflags grpc++ || true) 2>/dev/null)
GRPCPP_LIBS_L:=		$(shell (pkg-config --libs-only-L grpc++ || true) 2>/dev/null)
GRPCPP_LIBS_l:=		$(shell (pkg-config --libs-only-l grpc++ || true) 2>/dev/null)

# protobuf
PROTOBUF_CFLAGS:=	$(shell (pkg-config --cflags protobuf || true) 2>/dev/null)
PROTOBUF_LIBS_L:=	$(shell (pkg-config --libs-only-L protobuf || true) 2>/dev/null)
PROTOBUF_LIBS_l:=	$(shell (pkg-config --libs-only-l protobuf || true) 2>/dev/null)

# gflags
GFLAGS_CFLAGS:=		$(shell (pkg-config --cflags gflags || true) 2>/dev/null)
GFLAGS_LIBS_L:=		$(shell (pkg-config --libs-only-L gflags || true) 2>/dev/null)
GFLAGS_LIBS_l:=		$(shell (pkg-config --libs-only-l gflags || true) 2>/dev/null)

# Helper to check if a feature flag is #defined in pttbbs.conf for GNU Make
DEF_CHECK=	$(shell grep -Ewq "^[ \t]*\#[ \t]*define[ \t]*$$1" $(BBSCONF) 2>/dev/null && echo "YES")
USE_MBBSD_CXX:=	$(call DEF_CHECK,USE_MBBSD_CXX)

######################################

.DEFAULT_GOAL := all

.clang_complete:
	make CC='~/.vim/bin/cc_args.py clang' clean all

$(SRCROOT)/include/var.h:	$(SRCROOT)/mbbsd/var.c
	perl $(SRCROOT)/util/parsevar.pl < $(SRCROOT)/mbbsd/var.c > $(SRCROOT)/include/var.h


.PHONY: all .clang_complete ctags
