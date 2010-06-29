# $Id: Makefile 4384 2008-06-28 15:50:14Z wens $
SRCROOT=	../..
.include "$(SRCROOT)/pttbbs.mk"

PROGS=          angelbeats
PROGRAMS=	angelbeats abc
UTILDIR=	$(SRCROOT)/util
UTILOBJ=	$(UTILDIR)/util_var.o

LDLIBS+=$(SRCROOT)/common/bbs/libcmbbs.a \
	$(SRCROOT)/common/sys/libcmsys.a \
	$(SRCROOT)/common/osdep/libosdep.a

all:	${PROGRAMS}

.SUFFIXES: .c .cpp .o
.c.o:
	$(CC) $(CFLAGS) -c $*.c
.cpp.o:
	$(CXX) $(CXXFLAGS) -c $*.cpp

angelbeats: angelbeats.o 
	${CC} ${CFLAGS} ${LDFLAGS} -levent -o $@ $> $(UTILOBJ) $(LDLIBS)

abc: abc.o 
	${CC} ${CFLAGS} ${LDFLAGS} -levent -o $@ $> $(UTILOBJ) $(LDLIBS)

install: $(PROGS)
	install -d $(BBSHOME)/bin/
	install -c -m 755 $(PROGS) $(BBSHOME)/bin/
	mv -f $(BBSHOME)/bin/angelbeats $(BBSHOME)/bin/angelbeats.`date '+%m%d%H%M'`
	ln -sv $(BBSHOME)/bin/angelbeats.`date '+%m%d%H%M'` $(BBSHOME)/bin/angelbeats

clean:
	rm -f *~ ${PROGRAMS} angelbeats.o abc.o
