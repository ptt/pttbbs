# $Id: Makefile 4384 2008-06-28 15:50:14Z wens $
SRCROOT=	../..
.include "$(SRCROOT)/pttbbs.mk"

PROGRAMS=	angelbeats abc
UTILDIR=	$(SRCROOT)/util
UTILOBJ=	$(UTILDIR)/util_var.o

LDLIBS:=$(SRCROOT)/common/bbs/libcmbbs.a \
	$(SRCROOT)/common/sys/libcmsys.a \
	$(SRCROOT)/common/osdep/libosdep.a \
	-levent $(LDLIBS)

all:	${PROGRAMS}

.SUFFIXES: .c .cpp .o
.c.o:
	$(CC) $(CFLAGS) -c $*.c
.cpp.o:
	$(CXX) $(CXXFLAGS) -c $*.cpp

angelbeats: angelbeats.o 
	${CC} ${CFLAGS} ${LDFLAGS} -o $@ $> $(UTILOBJ) $(LDLIBS)

abc: abc.o 
	${CC} ${CFLAGS} ${LDFLAGS} -o $@ $> $(UTILOBJ) $(LDLIBS)

install: $(PROGRAMS)
	install -d $(BBSHOME)/bin/
	install -c -m 755 $(PROGRAMS) $(BBSHOME)/bin/
	mv -f $(BBSHOME)/bin/angelbeats $(BBSHOME)/bin/angelbeats.`date '+%m%d%H%M'`
	ln -sv $(BBSHOME)/bin/angelbeats.`date '+%m%d%H%M'` $(BBSHOME)/bin/angelbeats

clean:
	rm -f *~ ${PROGRAMS} angelbeats.o abc.o
