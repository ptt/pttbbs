BBSHOME?=	$(HOME)
BBSHOME?=	/home/bbs

SRCROOT?=	.
OSTYPE!=	uname

# Detect best compiler
#
CC:=		gcc
CXX:=		g++

CLANG!=		sh -c 'type clang >/dev/null 2>&1 && echo 1 || echo 0'
CCACHE!=	sh -c 'type ccache >/dev/null 2>&1 && echo 1 || echo 0'

.if defined(WITHOUT_CLANG)
CLANG:=
.elif $(CLANG)
CC:=		clang
CXX:=		clang++
.endif

.if $(CCACHE)
CC:=		ccache $(CC)
CXX:=		ccache $(CXX)
.endif

# Common build flags

PTT_WARN:=	-W -Wall -Wunused \
    		-Wno-missing-field-initializers -Wno-address-of-packed-member
PTT_CFLAGS:=	$(PTT_WARN) -pipe -DBBSHOME='"$(BBSHOME)"' -I$(SRCROOT)/include
PTT_CXXFLAGS:=	$(PTT_WARN) -pipe -DBBSHOME='"$(BBSHOME)"' -I$(SRCROOT)/include
PTT_LDFLAGS:=	-Wl,--as-needed
.if $(CLANG)
PTT_CFLAGS+=	-Qunused-arguments -Wno-parentheses-equality \
		-fcolor-diagnostics -Wno-invalid-source-encoding
PTT_CXXFLAGS+=	-Wno-invalid-source-encoding
.endif

# enable assert()
#PTT_CFLAGS+=	-DNDEBUG 

# Platform specific build flags

.if ${OSTYPE} == "Darwin"
PTT_CFLAGS+=	-I/opt/local/include -DNEED_SETPROCTITLE
PTT_CXXFLAGS+=	-I/opt/local/include
PTT_LDFLAGS+=	-L/opt/local/lib
PTT_LDLIBS+=	-liconv
.endif

.if ${OSTYPE} == "FreeBSD"
# FreeBSD特有的環境
PTT_CFLAGS+=	-I/usr/local/include
PTT_CXXFLAGS+=	-I/usr/local/include
PTT_LDFLAGS+=	-L/usr/local/lib
PTT_LDLIBS+=	-lkvm -liconv
.endif

# 若有定義 PROFILING
.if defined(PROFILING)
PTT_CFLAGS+=	-pg
PTT_CXXFLAGS+=	-pg
PTT_LDFLAGS+=	-pg
NO_OMITFP=	yes
NO_FORK=	yes
.endif

# 若有定義 DEBUG, 則在 CFLAGS內定義 DEBUG
.if defined(DEBUG)
GDB=		1
PTT_CFLAGS+=	-DDEBUG 
PTT_CXXFLAGS+=	-DDEBUG 
.endif

.if defined(GDB)
CFLAGS:=	-g -O0 $(PTT_CFLAGS)
CXXFLAGS:=	-g -O0 $(PTT_CXXFLAGS)
LDFLAGS:=	-O0 $(PTT_LDFLAGS)
LDLIBS:=	$(PTT_LDLIBS)
.else
CFLAGS:=	-g -Os $(PTT_CFLAGS) $(EXT_CFLAGS)
CXXFLAGS:=	-g -Os $(PTT_CXXFLAGS) $(EXT_CXXFLAGS)
LDFLAGS:=	-Os $(PTT_LDFLAGS)
LDLIBS:=	$(PTT_LDLIBS)

.if defined(OMITFP)
CFLAGS+=	-fomit-frame-pointer
CXXFLAGS+=	-fomit-frame-pointer
.endif
.endif

LDADD=		$(LDLIBS)

# 若有定義 NO_FORK, 則在 CFLAGS內定義 NO_FORK
.if defined(NO_FORK)
CFLAGS+=	-DNO_FORK
CXXFLAGS+=	-DNO_FORK
.endif

######################################
# Settings for common libraries

# This will attempt to compile a small test program that only calls strlcpy()
HAVE_STRLCPY != \
       echo '\#include <string.h>\nint main(){char b[4];strlcpy(b,"x",4);}' | \
       ${CC} -x c -o /dev/null - 2>/dev/null && echo 1 || echo 0

HAVE_STRLCAT != \
       echo '\#include <string.h>\nint main(){char b[4]="x";strlcat(b,"y",4);}' | \
       ${CC} -x c -o /dev/null - 2>/dev/null && echo 1 || echo 0

.if ${HAVE_STRLCPY} != 1
CFLAGS+= -DNEED_STRLCPY=1
CXXFLAGS+= -DNEED_STRLCPY=1
.endif
.if ${HAVE_STRLCAT} != 1
CFLAGS+= -DNEED_STRLCAT=1
CXXFLAGS+= -DNEED_STRLCAT=1
.endif

#######################################################################
# conditional configurations and optional modules
#######################################################################

BBSCONF:=       $(SRCROOT)/pttbbs.conf
DEF_PATTERN:=   ^[ \t]*\#[ \t]*define[ \t]*
DEF_CMD:=       grep -Ewq "${DEF_PATTERN}"
DEF_YES:=       && echo "YES" || echo ""

#libevent
LIBEVENT_CFLAGS!=	(pkg-config --cflags libevent_pthreads || true) 2>/dev/null
LIBEVENT_LIBS_L!=	(pkg-config --libs-only-L libevent_pthreads || true) 2>/dev/null
LIBEVENT_LIBS_l!=	(pkg-config --libs-only-l libevent_pthreads || true) 2>/dev/null

# grpc++
GRPCPP_CFLAGS!=		(pkg-config --cflags grpc++ || true) 2>/dev/null
GRPCPP_LIBS_L!=		(pkg-config --libs-only-L grpc++ || true) 2>/dev/null
GRPCPP_LIBS_l!=		(pkg-config --libs-only-l grpc++ || true) 2>/dev/null

# protobuf
PROTOBUF_CFLAGS!=	(pkg-config --cflags protobuf || true) 2>/dev/null
PROTOBUF_LIBS_L!=	(pkg-config --libs-only-L protobuf || true) 2>/dev/null
PROTOBUF_LIBS_l!=	(pkg-config --libs-only-l protobuf || true) 2>/dev/null

# gflags
GFLAGS_CFLAGS!=		(pkg-config --cflags gflags || true) 2>/dev/null
GFLAGS_LIBS_L!=		(pkg-config --libs-only-L gflags || true) 2>/dev/null
GFLAGS_LIBS_l!=		(pkg-config --libs-only-l gflags || true) 2>/dev/null

# pmake common
CLEANFILES+=	*~

# NetBSD pmake
MKLINT:=no
MKPROFILE:=no
MKPIC:=no
# Do not take warnings as errors
NOGCCERROR:=no

# FreeBSD make
WITHOUT_PROFILE:=yes

# Apply conditional configurations for NetBSD Makefiles in commons/,
# mbbsd/ or more directory
USE_MBBSD_CXX!= sh -c '${DEF_CMD}"USE_MBBSD_CXX" ${BBSCONF} ${DEF_YES}'

######################################

.MAIN: all

.clang_complete:
	make CC='~/.vim/bin/cc_args.py clang' clean all

$(SRCROOT)/include/var.h:	$(SRCROOT)/mbbsd/var.c
	perl $(SRCROOT)/util/parsevar.pl < $(SRCROOT)/mbbsd/var.c > $(SRCROOT)/include/var.h


.PHONY: .clang_complete ctags
