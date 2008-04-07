# $Id$
SRCROOT=	../..
.include "$(SRCROOT)/pttbbs.mk"

PROGRAMS=	fromd

LDLIBS+=  $(SRCROOT)/common/sys/libcmsys.a \
	$(SRCROOT)/common/bbs/libcmbbs.a

all:	${PROGRAMS}

.SUFFIXES: .c .cpp .o
.c.o:
	$(CCACHE) $(CC) $(CFLAGS) -c $*.c
.cpp.o:
	$(CCACHE) $(CXX) $(CFLAGS) -c $*.cpp

fromd: fromd.o ip_desc_db.o
	${CC} ${CFLAGS} ${LDFLAGS} -levent -o $@ $> $(LDLIBS)

clean:
	rm -f *~ ${PROGRAMS} fromd.o ip_desc_db.o
