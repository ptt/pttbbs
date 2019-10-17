# $Id: Makefile 4384 2008-06-28 15:50:14Z wens $
SRCROOT=	../..
.include "$(SRCROOT)/pttbbs.mk"

PROGS=          logind
PROGRAMS=	logind loginc
UTILDIR=	$(SRCROOT)/util
UTILOBJ=	$(UTILDIR)/util_var.o

LDLIBS+=$(SRCROOT)/common/bbs/libcmbbs.a \
	$(SRCROOT)/common/sys/libcmsys.a \
	$(SRCROOT)/common/osdep/libosdep.a \
	-levent

DATETIME!=	date '+%Y%m%d%H%M'

all:	${PROGRAMS}

.SUFFIXES: .c .cpp .o
.c.o:
	$(CC) $(CFLAGS) -c $*.c
.cpp.o:
	$(CXX) $(CXXFLAGS) -c $*.cpp

logind: logind.o 
	${CC} ${CFLAGS} ${LDFLAGS} -o $@ $> $(UTILOBJ) $(LDLIBS)

loginc: loginc.o 
	${CC} ${CFLAGS} ${LDFLAGS} -o $@ $> $(LDLIBS)

install: $(PROGS)
	install -d $(BBSHOME)/bin/
	install -s -p $(PROGS) $(BBSHOME)/bin/logind.$(DATETIME)
	objcopy --only-keep-debug --compress-debug-sections $(PROGS) $(BBSHOME)/bin/logind.$(DATETIME).debug
	cd $(BBSHOME)/bin && objcopy --add-gnu-debuglink=logind.$(DATETIME).debug logind.$(DATETIME)
	@ln -sfv logind.$(DATETIME) $(BBSHOME)/bin/logind

clean:
	rm -f *~ ${PROGRAMS} logind.o loginc.o
