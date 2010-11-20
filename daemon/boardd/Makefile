# $Id$
SRCROOT=	../..
.include "$(SRCROOT)/pttbbs.mk"

PROGRAMS=	boardd
UTILDIR=	$(SRCROOT)/util
UTILOBJ=	$(UTILDIR)/util_var.o

LDLIBS+=$(SRCROOT)/common/bbs/libcmbbs.a \
	$(SRCROOT)/common/sys/libcmsys.a \
	$(SRCROOT)/common/osdep/libosdep.a \
	-levent

all:	${PROGRAMS}

.SUFFIXES: .c .cpp .o
.c.o:
	$(CC) $(CFLAGS) -c $*.c
.cpp.o:
	$(CXX) $(CXXFLAGS) -c $*.cpp

boardd: boardd.o $(UTILOBJ)
	${CC} ${CFLAGS} ${LDFLAGS} -o $* $> $(LDLIBS)

clean:
	rm -f *~ ${PROGRAMS} boardd.o
