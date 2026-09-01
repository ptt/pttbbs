SRCROOT=	../..
include $(SRCROOT)/pttbbs.mk

PROG=	server
SRCS=	server.c
OBJS=	server.o
MAN=

CFLAGS+=	$(LIBEVENT_CFLAGS)
LDFLAGS+=	$(LIBEVENT_LIBS_L)

LDADD+=	$(LIBEVENT_LIBS_l)

.PHONY: all clean install

all: $(PROG)

$(PROG): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDADD)

clean:
	rm -f $(PROG) $(OBJS)

install:
