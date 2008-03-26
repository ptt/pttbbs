# $Id$
#SRCROOT=	..
SRCROOT=	../..
.include "$(SRCROOT)/pttbbs.mk"

PROGRAMS=	utmpserver utmpsync utmpserver2 utmpserver3 authserver
UTILDIR=	$(SRCROOT)/util
UTILOBJ=	$(UTILDIR)/util_stuff.o $(UTILDIR)/util_var.o $(UTILDIR)/util_file.o $(UTILDIR)/util_cache.o $(UTILDIR)/util_passwd.o $(UTILDIR)/util_record.o $(UTILDIR)/util_osdep.o $(UTILDIR)/util_args.o

all:	${PROGRAMS}

.SUFFIXES: .c .cpp .o
.c.o:
	$(CCACHE) $(CC) $(CFLAGS) -c $*.c
.cpp.o:
	$(CCACHE) $(CXX) $(CFLAGS) -c $*.cpp

utmpserver: utmpserver.o $(UTILOBJ)
	${CC} ${CFLAGS} ${LDFLAGS} -o $* $*.o $(UTILOBJ)
utmpserver2: utmpserver2.o friend.o $(UTILOBJ)
	${CXX} ${CFLAGS} ${LDFLAGS} -o $* $*.o $(UTILOBJ) friend.o
utmpserver3: utmpserver3.o friend.o $(UTILOBJ)
	${CXX} ${CFLAGS} ${LDFLAGS} -levent -o $* $*.o $(UTILOBJ) friend.o
utmpsync: utmpsync.o $(UTILOBJ)
	${CC} ${CFLAGS} ${LDFLAGS} -o $* $*.o $(UTILOBJ)

authserver: authserver.o $(UTILOBJ)
	${CC} ${CFLAGS} ${LDFLAGS} -lcrypt -levent -o $* $>

clean:
	rm -f *~ ${PROGRAMS} friend.o utmpserver.o utmpserver2.o utmpserver3.o utmpsync.o authserver.o
