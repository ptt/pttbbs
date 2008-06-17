# $Id$
SRCROOT=	../..
.include "$(SRCROOT)/pttbbs.mk"

PROGRAMS=	fromd fromc

LDLIBS+=  $(SRCROOT)/common/sys/libcmsys.a \
	$(SRCROOT)/common/bbs/libcmbbs.a

all:	${PROGRAMS}

.SUFFIXES: .c .cpp .o
.c.o:
	$(CC) $(CFLAGS) -c $*.c
.cpp.o:
	$(CXX) $(CXXFLAGS) -c $*.cpp

fromd: fromd.o ip_desc_db.o
	${CC} ${CFLAGS} ${LDFLAGS} -levent -o $@ $> $(LDLIBS)

fromc: fromc.o
	${CC} ${CFLAGS} ${LDFLAGS} -o $@ $> $(LDLIBS)

clean:
	rm -f *~ ${PROGRAMS} fromd.o ip_desc_db.o
