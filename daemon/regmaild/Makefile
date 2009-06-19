# $Id$
SRCROOT=	../..
.include "$(SRCROOT)/pttbbs.mk"

PROGS=          regmaild
PROGRAMS=	regmaild regmailc
UTILDIR=	$(SRCROOT)/util
UTILOBJ=	$(UTILDIR)/util_var.o

LDLIBS+=$(SRCROOT)/common/bbs/libcmbbs.a \
	$(SRCROOT)/common/sys/libcmsys.a \
	$(SRCROOT)/common/osdep/libosdep.a \
	-lsqlite3

all:	${PROGRAMS}

.SUFFIXES: .c .cpp .o
.c.o:
	$(CC) $(CFLAGS) -c $*.c
.cpp.o:
	$(CXX) $(CXXFLAGS) -c $*.cpp

regmaild: regmaild.o 
	${CC} ${CFLAGS} ${LDFLAGS} -levent -o $@ $> $(UTILOBJ) $(LDLIBS)

regmailc: regmailc.o 
	${CC} ${CFLAGS} ${LDFLAGS} -levent -o $@ $> $(LDLIBS)

install: $(PROGS)
	install -d $(BBSHOME)/bin/
	install -c -m 755 $(PROGS) $(BBSHOME)/bin/

# TODO FIXME
initemaildb: regmaild.c
	$(CC) -DINIT_MAIN $(CFLAGS) $(LDFLAGS) -o initemaildb regmaild.c $(LDLIBS)


clean:
	rm -f *~ ${PROGRAMS} regmaild.o regmailc.o
