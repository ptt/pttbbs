# $Id$
.include "../pttbbs.mk"

PROGRAMS=	utmpserver utmpsync utmpserver2 utmpserver3 authserver
UTILOBJ=	../util/util_stuff.o ../util/util_var.o ../util/util_file.o ../util/util_cache.o ../util/util_passwd.o ../util/util_record.o ../util/util_osdep.o ../util/util_args.o

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
