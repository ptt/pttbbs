# $Id$
.include "../pttbbs.mk"

PROGRAMS=	utmpserver utmpsync

all:	${PROGRAMS}

.for fn in ${PROGRAMS}
${fn}: ${fn}.c
	${CC} ${CFLAGS} ${LDFLAGS} -DPTTBBS_UTIL -o ${fn} ${fn}.c ../util/util_*.o
.endfor

clean:
	rm -f *~ ${PROGRAMS}
