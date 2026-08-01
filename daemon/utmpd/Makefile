SRCROOT=	../..
.include "$(SRCROOT)/pttbbs.mk"

PROGRAMS=	utmpsync utmpserver3
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

utmpserver3: utmpserver3.o friend.o
	${CXX} ${CXXFLAGS} ${LDFLAGS} -o $* $*.o friend.o $(LDLIBS) -levent
utmpsync: utmpsync.o $(UTILOBJ)
	${CC} ${CFLAGS} ${LDFLAGS} -o $* $*.o $(UTILOBJ) $(LDLIBS)

clean:
	rm -f *~ ${PROGRAMS} friend.o utmpserver3.o utmpsync.o
