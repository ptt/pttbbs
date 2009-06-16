# $Id$
#SRCROOT=	..
SRCROOT=	../..
.include "$(SRCROOT)/pttbbs.mk"

PROGRAMS=	utmpserver utmpsync utmpserver2 utmpserver3 authserver
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

utmpserver: utmpserver.o $(UTILOBJ)
	${CC} ${CFLAGS} ${LDFLAGS} -o $* $*.o $(UTILOBJ) $(LDLIBS)
utmpserver2: utmpserver2.o friend.o $(UTILOBJ)
	${CXX} ${CFLAGS} ${LDFLAGS} -o $* $*.o $(UTILOBJ) friend.o $(LDLIBS)
utmpserver3: utmpserver3.o friend.o $(UTILOBJ)
	${CXX} ${CXXFLAGS} ${LDFLAGS} -levent -o $* $*.o $(UTILOBJ) friend.o $(LDLIBS)
utmpsync: utmpsync.o $(UTILOBJ)
	${CC} ${CFLAGS} ${LDFLAGS} -o $* $*.o $(UTILOBJ) $(LDLIBS)

authserver: authserver.o $(UTILOBJ)
	${CC} ${CFLAGS} ${LDFLAGS} -lcrypt -levent -o $* $> $(LDLIBS)

clean:
	rm -f *~ ${PROGRAMS} friend.o utmpserver.o utmpserver2.o utmpserver3.o utmpsync.o authserver.o
