SRCROOT=	../..
include $(SRCROOT)/pttbbs.mk

SRCS:=	daemon.c file.c lock.c log.c net.c sort.c string.c time.c \
	crypt.c bcrypt.c record.c vector.c telnet.c vbuf.c vtkbd.c \
	utf8.c big5.c buffer.c thttp.c 2fa.c

LIB:=	libcmsys.a
OBJS:=	$(SRCS:.c=.o)

.PHONY: all clean install

all: $(LIB)

$(LIB): $(OBJS)
	$(AR) rcs $@ $^

big5.c: big5data.tar.bz2 big5_gen.py
	./big5_gen.py > big5.c

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

install:

clean:
	rm -f $(OBJS) $(LIB)
