# $Id$
SRCROOT=	../..
.include "$(SRCROOT)/pttbbs.mk"

PROGRAMS=	bpop3d
UTILDIR=	$(SRCROOT)/util
UTILOBJ=	$(UTILDIR)/util_var.o $(UTILDIR)/util_cache.o $(UTILDIR)/util_passwd.o

LDLIBS+=$(SRCROOT)/common/bbs/libcmbbs.a \
	$(SRCROOT)/common/sys/libcmsys.a \
	$(SRCROOT)/common/osdep/libosdep.a

all:	${PROGRAMS}

.SUFFIXES: .c .cpp .o
.c.o:
	$(CC) $(CFLAGS) -c $*.c
.cpp.o:
	$(CXX) $(CXXFLAGS) -c $*.cpp

bpop3d: bpop3d.o $(UTILOBJ)
	${CC} ${CFLAGS} ${LDFLAGS} -lcrypt -levent -o $* $> $(LDLIBS)

clean:
	rm -f *~ ${PROGRAMS} bpop3d.o
