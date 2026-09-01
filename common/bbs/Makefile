SRCROOT:=	../..
include $(SRCROOT)/pttbbs.mk

SRCS:=	log.c money.c names.c path.c time.c string.c fhdr_stamp.c cache.c \
	passwd.c filehdr.c banip.c search.c shm.c cgo.c message.c \
	aloha.c 2fa.c
LIB:=	libcmbbs.a
OBJS:=	$(SRCS:.c=.o)

.PHONY: all clean install

all:	$(SRCROOT)/include/var.h $(LIB)

$(LIB): $(OBJS)
	$(AR) rcs $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

install:

clean:
	rm -f $(OBJS) $(LIB)
