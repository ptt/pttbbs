# $Id$
.include "../pttbbs.mk"

PROGRAMS=	utmpserver utmpsync
UTILOBJ=	../util/util_stuff.o ../util/util_var.o ../util/util_file.o ../util/util_cache.o ../util/util_passwd.o ../util/util_record.o

all:	${PROGRAMS}

.SUFFIXES: .c .o
.c.o:	
	$(CCACHE) $(CC) $(CFLAGS) -c $*.c

utmpserver: utmpserver.o $(UTILOBJ)
	${CC} ${CFLAGS} ${LDFLAGS} -o $* $*.o $(UTILOBJ)
utmpsync: utmpsync.o $(UTILOBJ)
	${CC} ${CFLAGS} ${LDFLAGS} -o $* $*.o $(UTILOBJ)

clean:
	rm -f *~ ${PROGRAMS}
