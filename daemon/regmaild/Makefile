# $Id$
SRCROOT=	../..
.include "$(SRCROOT)/pttbbs.mk"

PROGS=          regmaild
PROGRAMS=	regmaild regmailc initemaildb
UTILDIR=	$(SRCROOT)/util
UTILOBJ=	$(UTILDIR)/util_var.o

LDLIBS+=$(SRCROOT)/common/bbs/libcmbbs.a \
	$(SRCROOT)/common/sys/libcmsys.a \
	$(SRCROOT)/common/osdep/libosdep.a \
	-levent \
	-lsqlite3 \
	-lstdc++

CXXFLAGS+=-std=c++17 \
	  -I$(SRCROOT)/common/fbs

all:	${PROGRAMS}

regmaild: regmaild.o verifydb_handler.o
	${CC} ${CFLAGS} ${LDFLAGS} -o $@ $> $(UTILOBJ) $(LDLIBS)

regmailc: regmailc.o verifydb_client.o
	${CC} ${CFLAGS} ${LDFLAGS} -o $@ $> $(LDLIBS)

install: $(PROGS)
	install -d $(BBSHOME)/bin/
	install -c -m 755 $(PROGS) $(BBSHOME)/bin/

initemaildb: regmaild.c
	$(CC) -DINIT_MAIN $(CFLAGS) $(LDFLAGS) -o initemaildb regmaild.c $(LDLIBS)

clean:
	rm -f ${PROGRAMS} *.o
