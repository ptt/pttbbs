# $Id: Makefile,v 1.1 2003/05/19 01:33:00 in2 Exp $

.include "../../pttbbs.mk"

CFLAGS+=	-I../../include `xmlrpc-c-config cgi-server --cflags`
LDFLAGS+=	`xmlrpc-c-config cgi-server --libs`

UTIL_OBJS=	\
	../../util/util_cache.o ../../util/util_record.o ../../util/util_passwd.o 
all:	pttpi.cgi

pttpi.cgi: pttpi.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o pttpi.cgi pttpi.c $(UTIL_OBJS)

clean:
	rm -f *.cgi *~
