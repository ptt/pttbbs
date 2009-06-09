# $Id: Makefile 4384 2008-06-28 15:50:14Z wens $
SRCROOT=	../..
.include "$(SRCROOT)/pttbbs.mk"

PROGRAMS=	logind loginc
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

logind: logind.o 
	${CC} ${CFLAGS} ${LDFLAGS} -levent -o $@ $> $(UTILOBJ) $(LDLIBS)

loginc: loginc.o 
	${CC} ${CFLAGS} ${LDFLAGS} -levent -o $@ $> $(LDLIBS)

clean:
	rm -f *~ ${PROGRAMS} logind.o loginc.o
